/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04
 * File: kernel/sched.c
 * Purpose: Preemptive scheduler implementation.
 */
#include "core_sched.h"
#include "mem_pmm.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"
#include "mem_vmm.h"
#include "core_timer.h"
#include "arch_irq.h"
#include "arch_idt.h"

/* P0fix1 BUG-0006 (A13-2): kthread stacks are 4 pages (16 KiB), see
 * core_kthread_create below. */
#define OC_KTHREAD_STACK_PAGES 4

/* Task table. */
static task_t g_tasks[MAX_TASKS];
static tid_t  g_next_tid = 1;
static task_t *g_current = NULL;

/* P4 fix: destroyed-tid hooks. Subsystems with tid waiters register
 * here so core_kthread_destroy can notify them. sync.c (semaphores) uses
 * this to remove dead tids from sem->waiters[] arrays. */
#define MAX_DESTROYED_HOOKS 4
static tid_destroyed_hook_fn g_destroyed_hooks[MAX_DESTROYED_HOOKS];
void core_kthread_register_destroyed_hook(tid_destroyed_hook_fn fn) {
    for (int i = 0; i < MAX_DESTROYED_HOOKS; i++) {
        if (g_destroyed_hooks[i] == NULL) {
            g_destroyed_hooks[i] = fn;
            return;
        }
    }
}
static task_t *g_idle_task = NULL;

/* Scheduler stats. */
static u64 g_total_switches = 0;
static u64 g_total_preemptions = 0;

/* Forward declarations. */
static void __attribute__((unused)) core_sched_switch_to(task_t *next);
static void idle_task_fn(void *arg);
static void task_entry_trampoline(void *arg);

/* Per-priority ready queues (array-based). */
static tid_t g_ready_queue[32][MAX_TASKS];  /* per-priority queue of tids */
static int   g_ready_count[32];             /* number of tasks in each queue */
static u32   g_ready_bitmap = 0;            /* bit i set = priority i has a ready task */

/* P2-23 FIX: ready_push must be called with interrupts disabled (cli),
 * matching the protection in ready_pop. Without this, an IRQ between
 * reading g_ready_count[p] and writing g_ready_queue[p][...] could
 * corrupt the queue. */
static void ready_push(tid_t tid) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    int p = g_tasks[tid].priority;
    if (p < 0 || p >= 32) p = TASK_PRIO_DEFAULT;
    if (g_ready_count[p] < MAX_TASKS) {
        g_ready_queue[p][g_ready_count[p]++] = tid;
        g_ready_bitmap |= (1u << p);
    }
    __asm__ volatile("pushq %0; popfq" : : "r"(flags));
}

static tid_t ready_pop_highest(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    if (g_ready_bitmap == 0) { if (flags & 0x200) __asm__ volatile("sti"); return 0; }
    int p;
    __asm__ volatile("bsfl %1, %0" : "=r"(p) : "r"(g_ready_bitmap));
    if (g_ready_count[p] == 0) {
        g_ready_bitmap &= ~(1u << p);
        if (flags & 0x200) __asm__ volatile("sti");
        return 0;
    }
    tid_t tid = g_ready_queue[p][0];
    for (int i = 1; i < g_ready_count[p]; i++)
        g_ready_queue[p][i-1] = g_ready_queue[p][i];
    g_ready_count[p]--;
    if (g_ready_count[p] == 0)
        g_ready_bitmap &= ~(1u << p);
    if (flags & 0x200) __asm__ volatile("sti");
    return tid;
}

/* WP-09-FIX BUG-008: pop the FIFO head of one specific priority level.
 * Used by the starvation guard to hand a lower-priority task a turn. */
static tid_t ready_pop_level(int p);

/* BUG-028: forward decl — core_sched_tick (WP-09-FIX BUG-004) now reaps too. */
static void core_sched_reap_exited(void);

/* WP-09-FIX BUG-008: ticks elapsed since the idle task (the shell) last
 * got a turn. Drives the starvation guard in core_sched_tick. */
static u64 g_ticks_since_idle_run = 0;

static tid_t ready_pop_level(int p) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    if (p < 0 || p >= 32 || g_ready_count[p] == 0) {
        if (flags & 0x200) __asm__ volatile("sti");
        return -1;
    }
    tid_t tid = g_ready_queue[p][0];
    for (int i = 1; i < g_ready_count[p]; i++)
        g_ready_queue[p][i-1] = g_ready_queue[p][i];
    g_ready_count[p]--;
    if (g_ready_count[p] == 0)
        g_ready_bitmap &= ~(1u << p);
    if (flags & 0x200) __asm__ volatile("sti");
    return tid;
}

/* WP-09-FIX BUG-004/BUG-007: mark a task EXITED and remove it from the
 * ready queue. Without the queue removal, core_sched_switch_to() would set
 * the task back to TASK_RUNNING on the next pop and keep running it on
 * an address space the reaper just destroyed. */
void core_sched_task_exited(tid_t tid) {
    if (tid <= 0 || tid >= MAX_TASKS) return;
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    if (g_tasks[tid].in_use) g_tasks[tid].state = TASK_EXITED;
    int p = g_tasks[tid].priority;
    if (p >= 0 && p < 32) {
        int w = 0;
        for (int i = 0; i < g_ready_count[p]; i++) {
            if (g_ready_queue[p][i] == tid) continue;
            g_ready_queue[p][w++] = g_ready_queue[p][i];
        }
        g_ready_count[p] = w;
        if (w == 0) g_ready_bitmap &= ~(1u << p);
    }
    /* Same notification contract as core_kthread_destroy: let subsystems with
     * tid waiters (semaphores, pipe wait queues) drop the dead tid from
     * their waiter lists. Called with interrupts OFF. */
    for (int h = 0; h < MAX_DESTROYED_HOOKS; h++) {
        if (g_destroyed_hooks[h]) g_destroyed_hooks[h](tid);
    }
    if (flags & 0x200) __asm__ volatile("sti");
}

/* BUG-0042 FIX: pin the task_t FPU-state offsets that arch_context_switch.S
 * hardcodes as OFF_FPU / OFF_FPU_SAVED. A mismatch here would silently
 * corrupt every context switch. */
_Static_assert(__builtin_offsetof(task_t, fpu_state) == 288,
               "task_t.fpu_state offset must match OFF_FPU in arch_context_switch.S");
_Static_assert(__builtin_offsetof(task_t, fpu_saved) == 800,
               "task_t.fpu_saved offset must match OFF_FPU_SAVED in arch_context_switch.S");
/* BUG-0103 FIX (A3-02): the switch-in path now RESTORES RFLAGS from
 * task_t.rflags; pin its offset the same way so a future struct change
 * cannot silently desync the assembly. */
_Static_assert(__builtin_offsetof(task_t, rflags) == 208,
               "task_t.rflags offset must match OFF_RFLAGS in arch_context_switch.S");

void core_sched_init(void) {
    /* BUG-0042 FIX: enable the FPU/SSE environment and make it
     * un-trapped for fxsave/fxrstor in the context switch path.
     *
     * CR4.OSFXSR=1 is REQUIRED for any SSE instruction to be legal at
     * all (and for fxsave/fxrstor to save XMM state); with it clear the
     * CPU raises #UD on the first SSE instruction - the pre-fix
     * reproduction crashed exactly this way.
     * CR4.OSXMMEXCPT=1 routes unmasked SIMD floating-point exceptions
     * to #XF instead of the legacy #UD.
     * CR0: EM=0 (no x87 emulation), TS=0 (no #NM on FP/SSE), MP=1
     * (FWAIT honours TS if it is ever set again).
     * Without EM=0/TS=0 the fxsave/fxrstor in arch_context_switch
     * would fault. */
    {
        u64 cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4) :: "memory");
        cr4 |=  (1ULL << 9);   /* CR4.OSFXSR = 1 */
        cr4 |=  (1ULL << 10);  /* CR4.OSXMMEXCPT = 1 */
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");

        u64 cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0) :: "memory");
        cr0 &= ~(1ULL << 2);  /* CR0.EM = 0 */
        cr0 &= ~(1ULL << 3);  /* CR0.TS = 0 */
        cr0 |=  (1ULL << 1);  /* CR0.MP = 1 */
        __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    }

    memset(g_tasks, 0, sizeof(g_tasks));
    memset(g_ready_queue, 0, sizeof(g_ready_queue));
    memset(g_ready_count, 0, sizeof(g_ready_count));
    g_ready_bitmap = 0;
    g_total_switches = 0;
    g_total_preemptions = 0;
    g_next_tid = 1;

    /* Create the idle task (tid 0). */
    g_idle_task = &g_tasks[0];
    g_idle_task->tid = 0;
    strncpy(g_idle_task->name, "idle", 31);
    g_idle_task->state = TASK_READY;
    g_idle_task->priority = TASK_PRIO_MAX;
    g_idle_task->in_use = 1;
    g_idle_task->cr3 = 0;  /* kernel address space */

    /* P3-19 FIX: allocate a single page for the idle task stack (removed
     * duplicate allocation that leaked 4 pages). */
    g_idle_task->stack_base = (u64*)mem_pmm_alloc_frame();
    g_idle_task->stack_size = PMM_PAGE_SIZE;

    /* Set up the idle task's saved state. */
    u64 *sp = (u64*)((u8*)g_idle_task->stack_base + g_idle_task->stack_size);
    /* P4 fix: stack alignment. The old code set rsp = stack_base + size,
     * which is 4096-byte aligned (also 16-byte aligned). But the SysV ABI
     * requires rsp = 16n+8 at function entry (the +8 accounts for the
     * return address that CALL would push). Our context switch does
     * `mov rsp,[saved]; push [rip]; ret` — the push decrements rsp by 8,
     * making it 16n, and after ret rsp = 16n (no return addr). So saved
     * rsp must be 16n+8 to make the function entry see 16n+8 - 8 = 16n.
     * Wait that's wrong. Let me reconsider.
     *
     * Actually, the context switch pushes rip then rets. After ret, the
     * popped rip is gone, rsp = saved - 8 (because push subtracted 8).
     * Hmm no — push then ret: push [rip] writes to saved-8, rsp=saved-8.
     * Then ret pops from rsp (= saved-8), rsp = saved. So at function
     * entry, rsp = saved. We want rsp = 16n+8, so saved = 16n+8.
     *
     * stack_base + size is 4096-aligned = 16-aligned = 16n. Subtract 8
     * to get 16n+8. This matches the regular core_kthread_create path
     * (line 176: sp = sp - 8).
     *
     * The old idle-task code skipped this -8, leaving rsp = 16n. Then
     * at function entry rsp = 16n (not 16n+8), which is wrong per ABI.
     * Most C code doesn't care (we use -mno-sse so no aligned SSE moves),
     * but hand-written asm or compiler intrinsics that assume ABI
     * alignment could fault on movaps. */
    sp = (u64*)((u8*)sp - 8);  /* P4 fix: 16n+8 alignment */
    /* The idle task starts at idle_task_fn. */
    g_idle_task->rip = (u64)(uintptr_t)idle_task_fn;
    g_idle_task->cs = 0x18;  /* kernel code segment */
    g_idle_task->rflags = 0x202;  /* IF=1 */
    g_idle_task->rsp = (u64)(uintptr_t)sp;
    g_idle_task->ss = 0x10;  /* kernel data segment */
    g_idle_task->entry = idle_task_fn;
    g_idle_task->arg = NULL;

    /* The trampoline approach: set up the stack so that when we
     * context-switch TO this task for the first time, it starts
     * executing task_entry_trampoline. */
    g_idle_task->rip = (u64)(uintptr_t)task_entry_trampoline;
    g_idle_task->rdi = (u64)(uintptr_t)g_idle_task;  /* arg = task pointer */

    g_current = g_idle_task;
    g_current->state = TASK_RUNNING;
}

static void task_entry_trampoline(void *arg) {
    task_t *t = (task_t*)arg;
    if (t && t->entry) {
        t->entry(t->arg);
    }
    /* Task returned: mark it as exited. */
    t->state = TASK_EXITED;
    /* Yield forever (the scheduler will clean up). */
    for (;;) {
        core_sched_yield();
    }
}

static void idle_task_fn(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("hlt");
    }
}

tid_t core_kthread_create(void (*fn)(void *arg), void *arg, const char *name, int prio) {
    /* Find a free slot. */
    tid_t tid = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (!g_tasks[i].in_use) {
            tid = i;
            break;
        }
    }
    if (tid < 0) return -1;

    task_t *t = &g_tasks[tid];
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->in_use = 1;
    t->state = TASK_READY;
    t->priority = (prio >= 0 && prio <= 31) ? prio : TASK_PRIO_DEFAULT;
    if (name) strncpy(t->name, name, 31);
    else { char nm[8]; strcpy(nm, "task"); strncpy(t->name, nm, 31); }
    t->entry = fn;
    t->arg = arg;
    t->cr3 = 0;  /* kernel address space (identity-mapped) */
    t->ticks_remaining = TIME_SLICE_TICKS;

    /* Allocate a stack. */
    /* P0fix1 BUG-0006 (A13-2): the TX path nests three ~1.5KB frame
     * buffers (net_tcp_send_raw -> net_ip_send -> eth_send) plus a DNS
     * resolve adds ~5KB of live stack, and an IRQ-context net_poll RX->ACK
     * response on top of a kthread needs ~6KB. With a 1-page (4 KiB)
     * stack, any kernel thread doing network I/O (e.g. the checkupdate
     * autoupdate thread) overflowed its stack into the neighbouring
     * physical page. Give every kthread a physically-contiguous 4-page
     * (16 KiB) stack so the worst observed chain (~11KB: kthread frames
     * + IRQ frames on the same stack) fits with headroom. */
    u64 stack_phys = mem_pmm_alloc_contig(OC_KTHREAD_STACK_PAGES);
    if (stack_phys == 0) {
        t->in_use = 0;
        return -1;
    }
    t->stack_base = (u64*)stack_phys;
    t->stack_size = (u64)OC_KTHREAD_STACK_PAGES * PMM_PAGE_SIZE;

    /* Set up saved register state. */
    u64 *sp = (u64*)((u8*)t->stack_base + t->stack_size);
    /* Align stack: ABI requires rsp = 16n+8 at function entry.
     * The context switch does: mov rsp,[saved]; push [rip]; ret.
     * After ret, rsp = saved value. We want saved = 16n+8. */
    sp = (u64*)((u8*)sp - 8);  /* leave 8 bytes for alignment */
    t->rip = (u64)(uintptr_t)task_entry_trampoline;
    t->cs = 0x18;
    t->rflags = 0x202;  /* IF=1 */
    t->rsp = (u64)(uintptr_t)sp;
    t->ss = 0x10;
    t->rdi = (u64)(uintptr_t)t;  /* arg to trampoline */

    /* Add to ready queue. */
    ready_push(tid);

    return tid;
}

int core_kthread_destroy(tid_t tid) {
    if (tid <= 0 || tid >= MAX_TASKS) return -1;
    if (!g_tasks[tid].in_use) return -1;
    /* P1-9 FIX: mark as EXITED but don't free immediately if it's the
     * current task (use-after-free: the context switch code reads from
     * the task struct). The scheduler will skip EXITED tasks. Free the
     * stack only if this is NOT the current task. */
    g_tasks[tid].state = TASK_EXITED;
    /* BUG-009 FIX: Remove the tid from ALL ready queues. The old code
     * only marked the task as EXITED but left its tid in the per-priority
     * ready queue. When ready_pop_highest later popped this tid, it would
     * try to context-switch to a task with a freed stack → use-after-free.
     * Now we scan all 32 priority queues and remove any matching tid. */
    {
        u64 flags;
        __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
        for (int p = 0; p < 32; p++) {
            for (int j = 0; j < g_ready_count[p]; j++) {
                if (g_ready_queue[p][j] == tid) {
                    /* Shift remaining entries down. */
                    for (int k = j; k < g_ready_count[p] - 1; k++) {
                        g_ready_queue[p][k] = g_ready_queue[p][k + 1];
                    }
                    g_ready_count[p]--;
                    j--; /* recheck this position (shifted entry) */
                    /* Clear bitmap if queue is now empty. */
                    if (g_ready_count[p] == 0) {
                        g_ready_bitmap &= ~(1u << p);
                    }
                }
            }
        }
        /* P4 fix: Notify subsystems (sync.c semaphores, pipe wait queues)
         * that this tid is being destroyed, so they can remove it from
         * their waiter lists. Without this, dead tids stay in sem->waiters[]
         * forever (taking up slots and eventually causing new waiters to
         * be rejected with "table full"). */
        for (int h = 0; h < MAX_DESTROYED_HOOKS; h++) {
            if (g_destroyed_hooks[h]) g_destroyed_hooks[h](tid);
        }
        if (flags & 0x200) __asm__ volatile("sti");
    }
    /* Remove from ready queue if it was there. */
    if (tid != (g_current ? g_current->tid : 0)) {
        g_tasks[tid].in_use = 0;
        if (g_tasks[tid].stack_base) {
            for (u64 f = 0; f < OC_KTHREAD_STACK_PAGES; f++)
                mem_pmm_free_frame((u64)(uintptr_t)g_tasks[tid].stack_base + f * PMM_PAGE_SIZE);
            g_tasks[tid].stack_base = NULL;
        }
    } else {
        /* Current task: will be freed after context switch. */
        g_tasks[tid].in_use = 1;  /* keep in_use so scheduler can find it */
    }
    return 0;
}

/* BUG-006 FIX: Set a task's priority (0=highest..31=lowest). */
int core_kthread_set_priority(tid_t tid, int priority) {
    if (tid < 0 || tid >= MAX_TASKS) return -1;
    if (!g_tasks[tid].in_use) return -1;
    if (priority < 0 || priority > 31) return -1;
    g_tasks[tid].priority = priority;
    return 0;
}

/* BUG-001 FIX (P0): Don't overwrite TASK_EXITED with TASK_BLOCKED.
 *
 * sys_exit2 sets current->state = TASK_EXITED then calls core_kthread_block()
 * in a loop. Without this guard, core_kthread_block would overwrite EXITED
 * with BLOCKED, so core_sched_reap_exited() (which only reaps TASK_EXITED)
 * would never reclaim the slot. After MAX_TASKS (32) user-program
 * exits, the task table is full and no new process can be created —
 * system user-mode functionality is permanently paralyzed.
 *
 * Fix: if the current task is already EXITED, leave the state alone and
 * just yield to the scheduler (which will run core_sched_reap_exited to
 * reclaim the slot, then pick the next ready task). */
int core_kthread_block(void) {
    if (!g_current) return -1;
    if (g_current->state != TASK_EXITED) {
        g_current->state = TASK_BLOCKED;
    }
    core_sched_yield();
    return 0;
}

int core_kthread_wake(tid_t tid) {
    if (tid <= 0 || tid >= MAX_TASKS) return -1;
    if (!g_tasks[tid].in_use) return -1;
    /* BUG-0100 FIX (A2-7): the BLOCKED check + READY transition +
     * ready_push must be atomic. Wakers run with interrupts enabled
     * (pipe wake paths in core_syscall.c); a timer tick between the
     * state check and ready_push would call ready_push without the
     * P2-23-required cli protection and could corrupt the ready
     * queue / double-enqueue the woken tid. */
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    if (!g_tasks[tid].in_use || g_tasks[tid].state != TASK_BLOCKED) {
        if (flags & 0x200) __asm__ volatile("sti");
        return -1;
    }
    /* Set state to READY before pushing to the ready queue. This prevents
     * a duplicate wake: without this, a second core_kthread_wake would still
     * see TASK_BLOCKED and push the tid again, causing a double-entry
     * in the ready queue and a lost-wakeup race. */
    g_tasks[tid].state = TASK_READY;
    ready_push(tid);
    if (flags & 0x200) __asm__ volatile("sti");
    return 0;
}

task_t *core_kthread_current(void) {
    return g_current;
}

tid_t core_kthread_current_tid(void) {
    return g_current ? g_current->tid : 0;
}

void core_kthread_list(void) {
    char buf[120];
    char n[20];
    screen_console_puts("TID  PRIO  STATE     CPU_TICKS  SWITCHES  NAME\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!g_tasks[i].in_use) continue;
        const char *st;
        switch (g_tasks[i].state) {
            case TASK_READY:   st = "READY";   break;
            case TASK_RUNNING: st = "RUNNING"; break;
            case TASK_BLOCKED: st = "BLOCKED"; break;
            case TASK_EXITED:  st = "EXITED";  break;
            default:           st = "???";     break;
        }
        strcpy(buf, "");
        u64_to_str((u64)i, n); strcpy(buf+strlen(buf), n);
        /* pad to 5 */
        while (strlen(buf) < 5) strcpy(buf+strlen(buf), " ");
        u64_to_str((u64)g_tasks[i].priority, n); strcpy(buf+strlen(buf), n);
        while (strlen(buf) < 11) strcpy(buf+strlen(buf), " ");
        strcpy(buf+strlen(buf), st);
        while (strlen(buf) < 21) strcpy(buf+strlen(buf), " ");
        u64_to_str(g_tasks[i].cpu_time_ticks, n); strcpy(buf+strlen(buf), n);
        while (strlen(buf) < 32) strcpy(buf+strlen(buf), " ");
        u64_to_str(g_tasks[i].switch_count, n); strcpy(buf+strlen(buf), n);
        while (strlen(buf) < 42) strcpy(buf+strlen(buf), " ");
        strcpy(buf+strlen(buf), g_tasks[i].name);
        screen_console_puts(buf);
        screen_console_putc('\n');
    }
}

/* WP-10-wp08fix1: export the live task table for SYS_PS (ush ps/top).
 * Writes at most `max` entries; returns the number written. Includes
 * every in-use task (kernel threads AND user processes - user processes
 * are tasks launched via user_task_launcher, so they appear here too). */
int core_sched_task_info_get(core_sched_task_info_t *out, int max) {
    if (!out || max <= 0) return -1;
    int n = 0;
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        if (!g_tasks[i].in_use) continue;
        out[n].tid            = i;
        out[n].state          = g_tasks[i].state;
        out[n].priority       = g_tasks[i].priority;
        out[n].cpu_time_ticks = g_tasks[i].cpu_time_ticks;
        strncpy(out[n].name, g_tasks[i].name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = 0;
        n++;
    }
    return n;
}

static void core_sched_switch_to(task_t *next) {
    task_t *old = g_current;
    if (old == next) return;

    g_total_switches++;
    next->switch_count++;

    /* If the old task was running, make it ready (unless it's blocked/exited). */
    if (old->state == TASK_RUNNING) {
        old->state = TASK_READY;
        ready_push(old->tid);
    }

    next->state = TASK_RUNNING;
    next->ticks_remaining = TIME_SLICE_TICKS;
    if (next == g_idle_task)
        g_ticks_since_idle_run = 0;  /* WP-09-FIX BUG-008 */
    g_current = next;

    /* P1-10 FIX: switch CR3 if the next task has a different address space.
     * User processes have cr3 = their user PML4; kernel threads have cr3 = 0
     * (meaning: use the kernel address space, which is already loaded). */
    if (next->cr3 != 0 && next->cr3 != old->cr3) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
    } else if (next->cr3 == 0 && old->cr3 != 0) {
        /* Switching from a user task back to a kernel task: reload
         * the kernel CR3. */
        extern unsigned long long mem_vmm_kernel_as(void);
        unsigned long long mem_vmm_kern_as = mem_vmm_kernel_as();
        __asm__ volatile("mov %0, %%cr3" :: "r"(mem_vmm_kern_as) : "memory");
    }

    /* BUG-029 FIX: Update TSS RSP0 on context switch.
     * When a ring-3 IRQ occurs, the CPU uses RSP0 from the TSS to
     * switch to the kernel stack. If RSP0 still points to the previous
     * task's stack, the IRQ handler runs on the wrong stack.
     * Now we update RSP0 to the next task's kernel stack. */
    if (next->cr3 != 0 && next->rsp0 != 0) {
        extern void arch_set_tss_rsp0(u64 rsp);
        arch_set_tss_rsp0(next->rsp0);
    }

    arch_context_switch(old, next);
}

void core_sched_tick(void) {
    if (!g_current) return;
    /* WP-09-FIX BUG-004: reap EXITED tasks from the timer tick as well,
     * so killed tasks release their tid/stack promptly even when the
     * shell never calls core_sched_yield (hlt-based readline). */
    core_sched_reap_exited();
    g_current->cpu_time_ticks++;
    if (g_current->ticks_remaining > 0)
        g_current->ticks_remaining--;

    /* BUG-002 FIX (b): When the time slice expires, UNCONDITIONALLY yield
     * to the next ready task — including the idle task (priority 31).
     *
     * The previous fix only switched to a task with higher-or-equal priority.
     * This was broken because the shell runs inside the idle task (priority
     * 31, the lowest). A CPU-bound user program like `loop` (priority 15)
     * would preempt the idle/shell, and when the loop's time slice expired,
     * the only ready task was the idle/shell (priority 31, LOWER than the
     * loop's 15). The old condition `p < g_current->priority` (31 < 15) was
     * FALSE, so the scheduler never switched back to the shell. The loop
     * ran forever and the shell was permanently unresponsive.
     *
     * The correct behavior (per FIX-005): when the time slice expires, yield
     * to the highest-priority READY task, even if it is lower priority than
     * the current task. This gives the idle/shell a turn to run. When a
     * higher-priority task becomes ready, it preempts immediately.
     *
     * Round-robin within the same priority: if the only ready task at the
     * current priority is the current task itself, re-queue it and switch
     * to the idle task so lower-priority tasks (the shell) get a turn. */

    int highest_p = 31;  /* default: lowest priority (only idle available) */
    if (g_ready_bitmap != 0) {
        __asm__ volatile("bsfl %1, %0" : "=r"(highest_p) : "r"(g_ready_bitmap));
    }

    int should_switch = 0;
    tid_t force_next = -1;
    if (g_current->ticks_remaining == 0) {
        /* Time slice expired: yield unconditionally. */
        should_switch = 1;
    } else if (g_ready_bitmap != 0 && highest_p < g_current->priority) {
        /* A higher-priority task is ready: preempt immediately. */
        should_switch = 1;
    } else {
        /* WP-09-FIX BUG-008: starvation guard.
         * Track how long the idle task (which hosts the interactive
         * shell) has gone WITHOUT being scheduled. Two equal-priority
         * CPU-bound user tasks (prio 15) bounce between themselves via
         * the time-slice-expiry path — the shell (idle, prio 31) never
         * wins ready_pop_highest() and starves forever. After
         * SCHED_STARVE_LIMIT ticks (1s @ 100 Hz) without an idle turn,
         * force-switch to the lowest-priority ready task, or to idle
         * itself when no lower-priority task is queued (idle is never
         * queued — it must be forced directly). */
        if (g_current != g_idle_task) {
            g_ticks_since_idle_run++;
            if (g_ticks_since_idle_run >= SCHED_STARVE_LIMIT) {
                int p;
                for (p = 31; p > g_current->priority; p--) {
                    if (g_ready_bitmap & (1u << p)) break;
                }
                if (p > g_current->priority) {
                    force_next = ready_pop_level(p);
                    if (force_next > 0) {
                        should_switch = 1;
                        g_ticks_since_idle_run = 0;
                    }
                }
                if (!should_switch) {
                    /* No lower-priority queued task: hand the CPU back to
                     * the idle/shell task directly (tid 0). */
                    force_next = 0;
                    should_switch = 1;
                    g_ticks_since_idle_run = 0;
                }
            }
        }
    }

    if (should_switch) {
        /* Push current back to ready queue (unless it IS the idle task —
         * the idle task is never queued; it runs implicitly when nothing
         * else is ready). */
        if (g_current != g_idle_task && g_current->state == TASK_RUNNING) {
            g_current->state = TASK_READY;
            ready_push(g_current->tid);
        }
        tid_t next_tid;
        if (force_next >= 0) {
            /* Starvation-guard switch: target already popped (tid 0 = idle
             * is never queued, so no pop happened for it). */
            next_tid = force_next;
        } else {
            next_tid = ready_pop_highest();
            /* If the popped task is the same as the current (only one task at
             * this priority level), re-add it to the queue and switch to idle
             * so that lower-priority tasks (the shell) get a turn. */
            if (next_tid == g_current->tid && g_current != g_idle_task) {
                ready_push(next_tid);
                next_tid = 0;  /* idle */
            }
        }
        if (next_tid != g_current->tid) {
            if (g_current->ticks_remaining == 0)
                g_total_preemptions++;
            g_tasks[next_tid].state = TASK_RUNNING;
            core_sched_switch_to(&g_tasks[next_tid]);
        } else {
            /* No other task to switch to. Reset time slice. */
            g_current->ticks_remaining = TIME_SLICE_TICKS;
        }
    }
}

/* BUG-028 FIX: Reap exited tasks (free stack + clear in_use).
 * Called from core_sched_yield AND core_sched_tick (WP-09-FIX BUG-004: the shell's
 * readline loop uses hlt and never yields, so relying on core_sched_yield alone
 * let EXITED zombies pile up and occupy tid slots for a long time). */
static void core_sched_reap_exited(void) {
    for (tid_t t = 1; t < MAX_TASKS; t++) {
        if (g_tasks[t].in_use && g_tasks[t].state == TASK_EXITED) {
            /* Don't reap the current task (we're on its stack). */
            if (&g_tasks[t] == g_current) continue;
            /* Free the stack. */
            if (g_tasks[t].stack_base) {
                for (u64 f = 0; f < OC_KTHREAD_STACK_PAGES; f++)
                    mem_pmm_free_frame((u64)(uintptr_t)g_tasks[t].stack_base + f * PMM_PAGE_SIZE);
                g_tasks[t].stack_base = NULL;
            }
            g_tasks[t].in_use = 0;
        }
    }
}

void core_sched_yield(void) {
    if (!g_current) return;
    /* BUG-028 FIX: Reap exited tasks before scheduling. */
    core_sched_reap_exited();
    /* Move current task to ready queue and pick the next one. */
    if (g_current->state == TASK_RUNNING) {
        g_current->state = TASK_READY;
        ready_push(g_current->tid);
    }
    tid_t next_tid = ready_pop_highest();
    /* BUG-0102 FIX (A3-01): if the pop returned OUR OWN tid (we were the
     * only/highest ready task), the old code called
     * core_sched_switch_to(&g_tasks[g_current->tid]), which returns
     * immediately (old == next) WITHOUT touching task state. We kept
     * running with state == TASK_READY and NOT in any ready queue, so
     * the next core_sched_tick saw state != TASK_RUNNING, refused to
     * re-queue us (core_sched_tick only pushes TASK_RUNNING currents),
     * and switched away - after that we were on no queue and in no
     * RUNNING state: an orphan, never scheduled again. Restore
     * TASK_RUNNING and keep the CPU instead (a yield with nobody else
     * ready is a no-op). */
    if (next_tid == g_current->tid) {
        g_current->state = TASK_RUNNING;
        return;
    }
    if (next_tid > 0) {
        core_sched_switch_to(&g_tasks[next_tid]);
    } else if (g_current->state != TASK_RUNNING) {
        /* Current task is blocked/exited, switch to idle. */
        core_sched_switch_to(g_idle_task);
    } else {
        /* BUG-0102 FIX (A3-01) second half: queue empty but we pushed
         * ourselves a moment ago and something already consumed the
         * entry (e.g. the tick interleaved inside this window). Our
         * state is TASK_READY but we are not queued - re-arm us as
         * RUNNING or we orphan the same way. */
        g_current->state = TASK_RUNNING;
    }
}

void core_sched_get_stats(core_sched_stats_t *out) {
    if (!out) return;
    out->total_switches = g_total_switches;
    out->total_preemptions = g_total_preemptions;
    out->current_tid = g_current ? g_current->tid : 0;
    out->active_tasks = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].in_use && g_tasks[i].state != TASK_EXITED)
            out->active_tasks++;
    }
}

void core_kthread_set_cr3(tid_t tid, u64 cr3) {
    if (tid < 0 || tid >= MAX_TASKS) return;
    if (!g_tasks[tid].in_use) return;
    g_tasks[tid].cr3 = cr3;
}

task_t *core_kthread_get_task(tid_t tid) {
    if (tid < 0 || tid >= MAX_TASKS) return NULL;
    if (!g_tasks[tid].in_use) return NULL;
    return &g_tasks[tid];
}
