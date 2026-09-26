/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04
 * File: kernel/sched.c
 * Purpose: Preemptive scheduler implementation.
 */
#include "sched.h"
#include "pmm.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "vmm.h"
#include "timer.h"
#include "irq.h"
#include "idt.h"

/* Task table. */
static task_t g_tasks[MAX_TASKS];
static tid_t  g_next_tid = 1;
static task_t *g_current = NULL;
static task_t *g_idle_task = NULL;

/* Scheduler stats. */
static u64 g_total_switches = 0;
static u64 g_total_preemptions = 0;

/* Forward declarations. */
static void __attribute__((unused)) sched_switch_to(task_t *next);
static void idle_task_fn(void *arg);
static void task_entry_trampoline(void *arg);

/* Per-priority ready queues (array-based). */
static tid_t g_ready_queue[32][MAX_TASKS];  /* per-priority queue of tids */
static int   g_ready_count[32];             /* number of tasks in each queue */
static u32   g_ready_bitmap = 0;            /* bit i set = priority i has a ready task */

static void ready_push(tid_t tid) {
    int p = g_tasks[tid].priority;
    if (p < 0 || p >= 32) p = TASK_PRIO_DEFAULT;
    if (g_ready_count[p] < MAX_TASKS) {
        g_ready_queue[p][g_ready_count[p]++] = tid;
        g_ready_bitmap |= (1u << p);
    }
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

void sched_init(void) {
    oc_memset(g_tasks, 0, sizeof(g_tasks));
    oc_memset(g_ready_queue, 0, sizeof(g_ready_queue));
    oc_memset(g_ready_count, 0, sizeof(g_ready_count));
    g_ready_bitmap = 0;
    g_total_switches = 0;
    g_total_preemptions = 0;
    g_next_tid = 1;

    /* Create the idle task (tid 0). */
    g_idle_task = &g_tasks[0];
    g_idle_task->tid = 0;
    oc_strncpy(g_idle_task->name, "idle", 31);
    g_idle_task->state = TASK_READY;
    g_idle_task->priority = TASK_PRIO_MAX;
    g_idle_task->in_use = 1;
    g_idle_task->cr3 = 0;  /* kernel address space */

    /* P3-19 FIX: allocate a single page for the idle task stack (removed
     * duplicate allocation that leaked 4 pages). */
    g_idle_task->stack_base = (u64*)pmm_alloc_frame();
    g_idle_task->stack_size = PMM_PAGE_SIZE;

    /* Set up the idle task's saved state. */
    u64 *sp = (u64*)((u8*)g_idle_task->stack_base + g_idle_task->stack_size);
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
        sched_yield();
    }
}

static void idle_task_fn(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("hlt");
    }
}

tid_t kthread_create(void (*fn)(void *arg), void *arg, const char *name, int prio) {
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
    oc_memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->in_use = 1;
    t->state = TASK_READY;
    t->priority = (prio >= 0 && prio <= 31) ? prio : TASK_PRIO_DEFAULT;
    if (name) oc_strncpy(t->name, name, 31);
    else { char nm[8]; oc_strcpy(nm, "task"); oc_strncpy(t->name, nm, 31); }
    t->entry = fn;
    t->arg = arg;
    t->cr3 = 0;  /* kernel address space (identity-mapped) */
    t->ticks_remaining = TIME_SLICE_TICKS;

    /* Allocate a stack (1 page = 4 KiB, enough for simple kernel threads). */
    u64 stack_phys = pmm_alloc_frame();
    if (stack_phys == 0) {
        t->in_use = 0;
        return -1;
    }
    t->stack_base = (u64*)stack_phys;
    t->stack_size = PMM_PAGE_SIZE;

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

int kthread_destroy(tid_t tid) {
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
        if (flags & 0x200) __asm__ volatile("sti");
    }
    /* Remove from ready queue if it was there. */
    if (tid != (g_current ? g_current->tid : 0)) {
        g_tasks[tid].in_use = 0;
        if (g_tasks[tid].stack_base) {
            pmm_free_frame((u64)(uintptr_t)g_tasks[tid].stack_base);
            g_tasks[tid].stack_base = NULL;
        }
    } else {
        /* Current task: will be freed after context switch. */
        g_tasks[tid].in_use = 1;  /* keep in_use so scheduler can find it */
    }
    return 0;
}

/* BUG-006 FIX: Set a task's priority (0=highest..31=lowest). */
int kthread_set_priority(tid_t tid, int priority) {
    if (tid < 0 || tid >= MAX_TASKS) return -1;
    if (!g_tasks[tid].in_use) return -1;
    if (priority < 0 || priority > 31) return -1;
    g_tasks[tid].priority = priority;
    return 0;
}

int kthread_block(void) {
    if (!g_current) return -1;
    g_current->state = TASK_BLOCKED;
    sched_yield();
    return 0;
}

int kthread_wake(tid_t tid) {
    if (tid <= 0 || tid >= MAX_TASKS) return -1;
    if (!g_tasks[tid].in_use) return -1;
    if (g_tasks[tid].state != TASK_BLOCKED) return -1;
    /* Set state to READY before pushing to the ready queue. This prevents
     * a duplicate wake: without this, a second kthread_wake would still
     * see TASK_BLOCKED and push the tid again, causing a double-entry
     * in the ready queue and a lost-wakeup race. */
    g_tasks[tid].state = TASK_READY;
    ready_push(tid);
    return 0;
}

task_t *kthread_current(void) {
    return g_current;
}

tid_t kthread_current_tid(void) {
    return g_current ? g_current->tid : 0;
}

void kthread_list(void) {
    char buf[120];
    char n[20];
    oc_console_puts("TID  PRIO  STATE     CPU_TICKS  SWITCHES  NAME\n");
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
        oc_strcpy(buf, "");
        oc_u64_to_str((u64)i, n); oc_strcpy(buf+oc_strlen(buf), n);
        /* pad to 5 */
        while (oc_strlen(buf) < 5) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_u64_to_str((u64)g_tasks[i].priority, n); oc_strcpy(buf+oc_strlen(buf), n);
        while (oc_strlen(buf) < 11) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_strcpy(buf+oc_strlen(buf), st);
        while (oc_strlen(buf) < 21) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_u64_to_str(g_tasks[i].cpu_time_ticks, n); oc_strcpy(buf+oc_strlen(buf), n);
        while (oc_strlen(buf) < 32) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_u64_to_str(g_tasks[i].switch_count, n); oc_strcpy(buf+oc_strlen(buf), n);
        while (oc_strlen(buf) < 42) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_strcpy(buf+oc_strlen(buf), g_tasks[i].name);
        oc_console_puts(buf);
        oc_console_putc('\n');
    }
}

static void sched_switch_to(task_t *next) {
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
    g_current = next;

    /* P1-10 FIX: switch CR3 if the next task has a different address space.
     * User processes have cr3 = their user PML4; kernel threads have cr3 = 0
     * (meaning: use the kernel address space, which is already loaded). */
    if (next->cr3 != 0 && next->cr3 != old->cr3) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
    } else if (next->cr3 == 0 && old->cr3 != 0) {
        /* Switching from a user task back to a kernel task: reload
         * the kernel CR3. */
        extern unsigned long long vmm_kernel_as(void);
        unsigned long long kern_as = vmm_kernel_as();
        __asm__ volatile("mov %0, %%cr3" :: "r"(kern_as) : "memory");
    }

    /* BUG-029 FIX: Update TSS RSP0 on context switch.
     * When a ring-3 IRQ occurs, the CPU uses RSP0 from the TSS to
     * switch to the kernel stack. If RSP0 still points to the previous
     * task's stack, the IRQ handler runs on the wrong stack.
     * Now we update RSP0 to the next task's kernel stack. */
    if (next->cr3 != 0 && next->rsp0 != 0) {
        extern void oc_set_tss_rsp0(u64 rsp);
        oc_set_tss_rsp0(next->rsp0);
    }

    oc_context_switch(old, next);
}

void sched_tick(void) {
    if (!g_current) return;
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
    if (g_current->ticks_remaining == 0) {
        /* Time slice expired: yield unconditionally. */
        should_switch = 1;
    } else if (g_ready_bitmap != 0 && highest_p < g_current->priority) {
        /* A higher-priority task is ready: preempt immediately. */
        should_switch = 1;
    }

    if (should_switch) {
        /* Push current back to ready queue (unless it IS the idle task —
         * the idle task is never queued; it runs implicitly when nothing
         * else is ready). */
        if (g_current != g_idle_task && g_current->state == TASK_RUNNING) {
            g_current->state = TASK_READY;
            ready_push(g_current->tid);
        }
        tid_t next_tid = ready_pop_highest();
        /* If the popped task is the same as the current (only one task at
         * this priority level), re-add it to the queue and switch to idle
         * so that lower-priority tasks (the shell) get a turn. */
        if (next_tid == g_current->tid && g_current != g_idle_task) {
            ready_push(next_tid);
            next_tid = 0;  /* idle */
        }
        if (next_tid != g_current->tid) {
            if (g_current->ticks_remaining == 0)
                g_total_preemptions++;
            g_tasks[next_tid].state = TASK_RUNNING;
            sched_switch_to(&g_tasks[next_tid]);
        } else {
            /* No other task to switch to. Reset time slice. */
            g_current->ticks_remaining = TIME_SLICE_TICKS;
        }
    }
}

/* BUG-028 FIX: Reap exited tasks (free stack + clear in_use).
 * Called from sched_yield to clean up zombie tasks that are not the
 * current task. Without this, exited tasks accumulate and fill the
 * 32-slot task table, eventually causing kthread_create to fail. */
static void sched_reap_exited(void) {
    for (tid_t t = 1; t < MAX_TASKS; t++) {
        if (g_tasks[t].in_use && g_tasks[t].state == TASK_EXITED) {
            /* Don't reap the current task (we're on its stack). */
            if (&g_tasks[t] == g_current) continue;
            /* Free the stack. */
            if (g_tasks[t].stack_base) {
                pmm_free_frame((u64)(uintptr_t)g_tasks[t].stack_base);
                g_tasks[t].stack_base = NULL;
            }
            g_tasks[t].in_use = 0;
        }
    }
}

void sched_yield(void) {
    if (!g_current) return;
    /* BUG-028 FIX: Reap exited tasks before scheduling. */
    sched_reap_exited();
    /* Move current task to ready queue and pick the next one. */
    if (g_current->state == TASK_RUNNING) {
        g_current->state = TASK_READY;
        ready_push(g_current->tid);
    }
    tid_t next_tid = ready_pop_highest();
    if (next_tid > 0) {
        sched_switch_to(&g_tasks[next_tid]);
    } else if (g_current->state != TASK_RUNNING) {
        /* Current task is blocked/exited, switch to idle. */
        sched_switch_to(g_idle_task);
    }
}

void sched_get_stats(sched_stats_t *out) {
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

void kthread_set_cr3(tid_t tid, u64 cr3) {
    if (tid < 0 || tid >= MAX_TASKS) return;
    if (!g_tasks[tid].in_use) return;
    g_tasks[tid].cr3 = cr3;
}

task_t *kthread_get_task(tid_t tid) {
    if (tid < 0 || tid >= MAX_TASKS) return NULL;
    if (!g_tasks[tid].in_use) return NULL;
    return &g_tasks[tid];
}
