/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-08a
 * File: kernel/syscall.c
 * Purpose: Complete syscall set — process management, IPC, signals, memory,
 *          file/directory, and I/O multiplexing.
 */
#include "core_syscall.h"
#include "core_usermode.h"
#include "core_sched.h"
#include "mem_vmm.h"
#include "mem_pmm.h"
#include "mem_heap.h"
#include "fs_vfs.h"
#include "screen_console.h"
#include "screen_console_in.h"
#include "lib_string.h"
#include "driver_input_keyboard.h"
#include "shell.h"
#include "arch_idt.h"
#include "core_timer.h"   /* P2-04: core_timer_now_ms() for sys_uptime */

/* WP-08b Batch 5: libfoo.so bytes are defined in usermode.c (which
 * #includes solib_data.h). We just reference them here so sys_map_solib
 * can copy them into user address space. Including solib_data.h directly
 * here would cause a duplicate-definition linker error (both usermode.o
 * and syscall.o would define solib_libfoo). */
extern const u8 solib_libfoo[];
extern const u64 solib_libfoo_size;

#define USER_LIMIT 0x0000800000000000ULL

static int valid_user_ptr(u64 addr) { return addr != 0 && addr < USER_LIMIT; }
static int valid_user_buf(u64 addr, u64 len) {
    if (addr >= USER_LIMIT) return 0;
    if (len > 65536) return 0;
    if (addr + len > USER_LIMIT) return 0;
    return 1;
}

/* P3-9 FIX: access_ok — range + mapping check.
 *
 * The old `valid_user_buf` only checked that [addr, addr+len) lay
 * below USER_LIMIT. It did NOT verify that every page in the range
 * was actually mapped in the caller's address space. If a user
 * program passed an unmapped pointer (NULL, a freed-mmap address,
 * or a stack guard zone), the kernel would #PF when dereferencing
 * it → kill the kernel or the user process with no diagnostic.
 *
 * access_ok walks every 4 KiB page in [addr, addr+len) and calls
 * mem_vmm_is_mapped on each. Returns 1 if all pages are mapped, 0
 * otherwise. Callers should treat 0 as -EINVAL.
 *
 * We also implement copy_from_user / copy_to_user — for now these
 * are direct memcpy (the page-walk above guarantees safety). Future
 * hardening could replace the memcpy with a byte-wise copy that
 * catches a #PF in the middle (e.g. if the page is unmapped between
 * the access_ok check and the copy under concurrent munmap). */
static int access_ok_read(u64 addr, u64 len) {
    if (!valid_user_buf(addr, len)) return 0;
    user_proc_t *proc = user_process_current();
    if (!proc || !proc->as) return 0;  /* no AS — fail closed */
    /* Walk every page that the buffer touches. */
    u64 start = addr & ~0xFFFULL;
    u64 end = addr + len;
    if (end < addr) return 0;  /* overflow */
    for (u64 a = start; a < end; a += 0x1000) {
        u64 phys;
        if (!mem_vmm_is_mapped(proc->as, a, &phys)) return 0;
    }
    return 1;
}

/* access_ok for a NUL-terminated string in user memory. We can't
 * know len ahead of time, so we walk pages from addr until we either
 * find a NUL byte or hit USER_LIMIT / an unmapped page. */
static int access_ok_str(u64 addr) {
    if (!valid_user_ptr(addr)) return 0;
    user_proc_t *proc = user_process_current();
    if (!proc || !proc->as) return 0;
    u64 a = addr & ~0xFFFULL;
    while (a < USER_LIMIT) {
        u64 phys;
        if (!mem_vmm_is_mapped(proc->as, a, &phys)) return 0;
        /* Scan the page for a NUL byte. */
        const u8 *p = (const u8*)(a + (addr > a ? (addr - a) : 0));
        u64 scan_end = a + 0x1000;
        for (const u8 *q = p; q < (const u8*)scan_end; q++) {
            if (*q == 0) return 1;  /* found NUL — string is mapped */
        }
        a += 0x1000;
        addr = a;  /* subsequent pages start at offset 0 */
        if (a - (addr & ~0xFFFULL) > 4096) break;  /* sanity */
    }
    return 0;  /* ran off the end without a NUL */
}

/* copy_from_user: copy bytes from user VA to kernel buffer.
 * Returns 0 on success, -1 on failure (unmapped page mid-copy). */
static int copy_from_user(void *dst, u64 src, u64 len) {
    if (!access_ok_read(src, len)) return -1;
    memcpy(dst, (const void*)(uintptr_t)src, len);
    return 0;
}

/* copy_to_user: copy bytes from kernel buffer to user VA. */
static int copy_to_user(u64 dst, const void *src, u64 len) {
    if (!access_ok_read(dst, len)) return -1;  /* write needs same mapping */
    memcpy((void*)(uintptr_t)dst, src, len);
    return 0;
}

/* P5 fix: resolve a user-supplied path against the shell's cwd.
 * The VFS requires absolute paths (fs_vfs_normalize rejects paths not
 * starting with '/'). User programs like ush pass relative paths
 * (e.g. "test.txt") which the kernel must resolve against the cwd
 * before passing to fs_vfs_open/fs_vfs_stat/fs_vfs_readdir/etc.
 *
 * shell_resolve_path prepends g_cwd to relative paths and normalizes
 * the result. We then copy the resolved absolute path into a kernel
 * buffer so subsequent VFS calls don't touch user memory.
 *
 * Returns:
 *   0 on success — `out` contains the resolved absolute path
 *  -1 on failure — path is NULL, unmapped, or too long */
static int resolve_user_path(u64 user_path, char *out, int out_len) {
    if (!access_ok_str(user_path)) return -1;
    /* First copy the user path into a local buffer. */
    char user_buf[256];
    {
        const char *p = (const char*)(uintptr_t)user_path;
        u64 n = 0;
        while (n < 255 && p[n] != '\0') n++;
        if (copy_from_user(user_buf, user_path, n + 1) != 0) return -1;
        user_buf[n] = 0;
    }
    /* BUG-0278 FIX (A2-14): resolve against the CALLER's per-process cwd
     * (copied in at fork, seeded from the kernel shell at spawn time,
     * changed only by this process's own chdir). The old code resolved
     * every relative path against the kernel shell's single global
     * g_cwd, so any process's chdir moved the path base of ALL
     * processes - relative opens/stat/mkdirs could land in a directory
     * the caller never selected. */
    user_proc_t *proc = user_process_current();
    if (proc) {
        extern int shell_resolve_path_base(const char *cwd, const char *path,
                                           char *out, int out_len);
        if (shell_resolve_path_base(proc->cwd, user_buf, out, out_len) < 0) return -1;
        return 0;
    }
    /* Defensive fallback (no current user process): kernel shell cwd. */
    extern int shell_resolve_path(const char *path, char *out, int out_len);
    if (shell_resolve_path(user_buf, out, out_len) < 0) return -1;
    return 0;
}

/* P3-12 placeholder — real implementation arrives in Batch 4.
 * Kept as a non-static stub so the table registration works.
 * The forward declaration is moved below sys_sigaction. */

/* ---- Pipe implementation ---- */
#define PIPE_BUF_SIZE 4096
#define MAX_PIPES 32

/* P3-15 FIX: Wait queue for pipe readers/writers.
 * Old code stored a single tid in `reader_waiting` / `writer_waiting`.
 * If two readers blocked on the same pipe, only the second's tid was
 * recorded — the first was never woken (lost forever, leaked the
 * thread slot, deadlocked the system under any 2-reader scenario).
 *
 * Now we use a small fixed-size array of tids. PIPE_WAIT_QUEUE_MAX
 * is the maximum number of threads that can block on one pipe end
 * simultaneously. 8 is plenty for typical pipelines (e.g. `a|b|c`
 * has at most 2 readers per pipe end). */
#define PIPE_WAIT_QUEUE_MAX 8
typedef struct {
    u8 buf[PIPE_BUF_SIZE];
    u32 read_pos;
    u32 write_pos;
    int in_use;
    int reader_count;
    int writer_count;
    /* P3-15: wait queues (arrays of tids, -1 = empty slot). */
    int reader_waiters[PIPE_WAIT_QUEUE_MAX];
    int writer_waiters[PIPE_WAIT_QUEUE_MAX];
} sys_pipe_t;

static sys_pipe_t g_pipes[MAX_PIPES];

static sys_pipe_t *pipe_get(int pipe_id) {
    if (pipe_id < 1 || pipe_id > MAX_PIPES) return NULL;
    if (!g_pipes[pipe_id - 1].in_use) return NULL;
    return &g_pipes[pipe_id - 1];
}

static int pipe_alloc(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!g_pipes[i].in_use) {
            memset(&g_pipes[i], 0, sizeof(g_pipes[i]));
            g_pipes[i].in_use = 1;
            for (int j = 0; j < PIPE_WAIT_QUEUE_MAX; j++) {
                g_pipes[i].reader_waiters[j] = -1;
                g_pipes[i].writer_waiters[j] = -1;
            }
            return i + 1;
        }
    }
    return 0;
}

static u32 pipe_data_avail(sys_pipe_t *p) { return p->write_pos - p->read_pos; }
static u32 pipe_space_avail(sys_pipe_t *p) { return PIPE_BUF_SIZE - pipe_data_avail(p); }

/* P3-15: Wait-queue helpers. Add a tid to the queue (first free slot).
 * Wake-one: wake the FIRST waiter in queue order (FIFO). Wake-all:
 * wake every waiter (used when the pipe is closed). */
static void pipe_wait_add(int *queue, int tid) {
    for (int i = 0; i < PIPE_WAIT_QUEUE_MAX; i++) {
        if (queue[i] == -1) { queue[i] = tid; return; }
    }
    /* Queue full — caller will fall through to busy-loop. Should be
     * rare in practice (would require >8 threads blocked on one pipe). */
}
static void pipe_wait_remove(int *queue, int tid) {
    for (int i = 0; i < PIPE_WAIT_QUEUE_MAX; i++) {
        if (queue[i] == tid) { queue[i] = -1; return; }
    }
}
static void pipe_wake_one(int *queue) {
    for (int i = 0; i < PIPE_WAIT_QUEUE_MAX; i++) {
        if (queue[i] != -1) {
            core_kthread_wake(queue[i]);
            queue[i] = -1;
            return;
        }
    }
}
static void pipe_wake_all(int *queue) {
    for (int i = 0; i < PIPE_WAIT_QUEUE_MAX; i++) {
        if (queue[i] != -1) {
            core_kthread_wake(queue[i]);
            queue[i] = -1;
        }
    }
}

/* P3-11: Exposed pipe-close helper. Called from user_process_reap_resources
 * (defined in usermode.c) which can't see sys_pipe_t (file-local here).
 * kind: 2 = reader, 3 = writer. Decrements count and wakes any waiters. */
void pipe_close_fd(int pipe_id, int kind) {
    sys_pipe_t *p = pipe_get(pipe_id);
    if (!p) return;
    if (kind == 2) p->reader_count--;
    else           p->writer_count--;
    /* P3-15: wake ALL waiters on pipe close (data may now be readable
     * because writer_count==0, or writable because reader_count==0). */
    pipe_wake_all(p->reader_waiters);
    pipe_wake_all(p->writer_waiters);
}

static int sys_proc_fd_alloc(user_proc_t *proc) {
    for (int i = 3; i < PROC_MAX_FDS; i++) {
        if (proc->fds[i].kind == 0) { proc->fds[i].kind = 1; return i; }
    }
    return -1;
}

/* ============================================================
 * SYS_GETPID / SYS_GETPPID / SYS_EXIT2
 * ============================================================ */
static u64 sys_getpid(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    user_proc_t *p = user_process_current();
    return p ? (u64)p->pid : 0;
}

static u64 sys_getppid(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    user_proc_t *p = user_process_current();
    return p ? (u64)p->ppid : 0;
}

static u64 sys_exit2(u64 code, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    user_proc_t *p = user_process_current();
    if (p) {
        /* WP-08cd: Clear usershell running flag ONLY when the ush
         * process itself exits (not when pipe children exit).
         * The ush process has no parent user process (parent_tid=0
         * because it was started from the kernel shell). Pipe children
         * have parent_tid = ush's tid (> 0), so they don't clear it. */
        /* BUG-0290: the flag is now volatile at its definition (it is
         * polled by the kernel shell's sti/hlt spin in another task);
         * this declaration must spell the same qualifier to stay
         * type-consistent with the definition (plain `extern int`
         * against a `volatile int` object is undefined behaviour). */
        extern volatile int g_usershell_running;
        if (p->parent_tid == 0) {
            g_usershell_running = 0;
        }
        /* P3-11 FIX: Use the unified reaper so SIGKILL/exception/exit
         * all share the same cleanup logic (close fds + destroy AS). */
        user_process_reap_resources(p, (int)code);
        if (p->parent_tid > 0) core_kthread_wake(p->parent_tid);
    }
    task_t *t = core_kthread_current();
    if (t) t->state = TASK_EXITED;
    for (;;) core_kthread_block();
    return 0;
}

/* ============================================================
 * SYS_FORK (10) — true POSIX fork
 * ============================================================ */
static u64 sys_fork(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    user_proc_t *parent = user_process_current();
    if (!parent) return (u64)-1;

    u64 *frame = user_get_current_frame();
    if (!frame) return (u64)-1;
    arch_irq_frame_t *f = (arch_irq_frame_t*)frame;

    int child_idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!g_procs[i].alive) { child_idx = i; break; }
    }
    if (child_idx < 0) return (u64)-1;

    user_proc_t *child = &g_procs[child_idx];
    memset(child, 0, sizeof(*child));
    /* BUG-047 FIX: Use the global monotonic PID counter instead of
     * parent->pid * 100 + child_idx. The old formula could collide
     * with g_next_pid (e.g., if g_next_pid reaches 101, it would
     * collide with a child of pid=1). Now we use a shared counter
     * that guarantees uniqueness. */
    extern int g_next_pid;
    child->pid = g_next_pid++;
    child->ppid = parent->pid;
    child->alive = 1;
    child->parent_tid = parent->tid;
    child->brk = parent->brk;
    /* BUG-0274 FIX (A2-10): inherit the per-process allocator cursors.
     * The child's address space is a full deep copy of the parent's
     * (including mmap'd pages and dlopen'd .so images), but the old code
     * left mmap_base/next_solib_addr zeroed, so the child's first mmap
     * restarted at USER_MMAP_BASE and its first dlopen at USER_SOLIB_BASE
     * - on top of the inherited pages. map_user_pages overwrites PTEs
     * without freeing, so the inherited frames leaked and the inherited
     * data was silently replaced. POSIX fork clones the address space
     * AND its allocation state, so copy both cursors. */
    child->mmap_base = parent->mmap_base;
    child->next_solib_addr = parent->next_solib_addr;
    /* BUG-0278 FIX (A2-14): fork inherits the parent's per-process cwd. */
    memcpy(child->cwd, parent->cwd, USER_CWD_LEN);
    strncpy(child->name, parent->name, 31);

    child->as = copy_user_address_space(parent->as);
    if (child->as == 0) { child->alive = 0; return (u64)-1; }

    child->is_fork_child = 1;
    child->fork_rip = f->rip;
    child->fork_rsp = f->rsp;
    child->fork_rflags = f->rflags;
    child->fork_rax = 0;
    /* WP-08cd: Copy the full IRQ frame into proc struct.
     * The struct has 22 u64 fields (r15-rax, int_no, error_code, rip-ss). */
    {
        u64 *src = (u64*)frame;
        for (int i = 0; i < 22; i++)
            child->fork_regs[i] = src[i];
        /* Override rax to 0 (fork return value for child) */
        child->fork_regs[14] = 0;
    }

    for (int i = 0; i < NSIG; i++)
        child->sig_handlers[i] = parent->sig_handlers[i];

    for (int i = 0; i < PROC_MAX_FDS; i++) {
        child->fds[i] = parent->fds[i];
        if (child->fds[i].kind == 2) {
            sys_pipe_t *p = pipe_get(child->fds[i].pipe_id);
            if (p) p->reader_count++;
        } else if (child->fds[i].kind == 3) {
            sys_pipe_t *p = pipe_get(child->fds[i].pipe_id);
            if (p) p->writer_count++;
        } else if (child->fds[i].kind == 1) {
            /* BUG-0098 FIX (A2-5): the child shares the parent's VFS
             * open-file slot. Add a reference so a later close by EITHER
             * side only drops its own reference instead of destroying
             * the slot (which left the survivor with a dangling fd whose
             * reads failed and writes were silently lost). */
            fs_vfs_fd_addref(child->fds[i].fs_vfs_fd);
        }
    }

    child->tid = core_kthread_create(user_task_launcher, child, child->name, TASK_PRIO_DEFAULT);
    if (child->tid < 0) { child->alive = 0; return (u64)-1; }
    core_kthread_set_cr3(child->tid, child->as);
    /* Kernel-#UD root-cause fix (T1/T4/T6-collect crash investigation).
     * user_process_create sets task_t.rsp0 for exec-launched processes
     * (the BUG-029 TSS fix), but this fork path never did, so the forked
     * child's task kept rsp0 = 0. core_sched_switch_to skips the TSS
     * update when next->rsp0 == 0, which left the TSS pointing at the
     * PARENT's kernel-stack top. While the child ran in ring 3, every
     * interrupt then pushed its frame - and, if the tick's scheduler
     * switched away, the whole core_sched_tick -> core_sched_switch_to ->
     * arch_context_switch chain - onto the PARENT's kernel stack, saving
     * the child's rsp/rip there (observed live: child task rsp=0x643EE0
     * inside the parent's 0x63E000..0x644000 stack window). When the
     * parent later resumed, its stack slots had been overwritten by the
     * child's kernel activity, and the next `ret` popped child data
     * (&g_procs[1].fork_rsp / &g_procs[1].pid / &g_procs[1].sig_handlers
     * [10]+1 - the three #UD rip values captured in the crash dumps) as
     * a return address: kernel #UD inside g_procs[] right after a child
     * exits, in the T1 tri-generation fork, T4 post-signal exit2 and T6
     * collect configurations. The same stack-crossing is the pre-existing
     * baseline T2 freeze (silent corruption without the #UD). Set the
     * child's rsp0 exactly like the exec path does. */
    {
        task_t *ct = core_kthread_get_task(child->tid);
        if (ct) ct->rsp0 = (u64)(uintptr_t)ct->stack_base + ct->stack_size;
    }

    /* BUG-0042 FIX: inherit the parent's FPU/SSE state. The parent may
     * hold newer FPU data in the live registers than its task_t image
     * (which is only refreshed on switch-out), so fxsave the live state
     * first, then copy it into the child's task_t. The child therefore
     * observes the same FPU/SSE values the parent had at fork(), as a
     * real address-space clone must. */
    {
        task_t *pt = core_kthread_current();
        task_t *ct = core_kthread_get_task(child->tid);
        if (pt && ct) {
            __asm__ volatile("fxsave %0" : "=m"(pt->fpu_state) :: "memory");
            pt->fpu_saved = 1;
            memcpy(ct->fpu_state, pt->fpu_state, sizeof(ct->fpu_state));
            ct->fpu_saved = 1;
        }
    }

    return (u64)child->pid;
}

/* ============================================================
 * SYS_WAIT4 (12)
 * ============================================================ */
static u64 sys_wait4(u64 pid_arg, u64 status_ptr, u64 options, u64 a4) {
    (void)options;(void)a4;
    /* P3-10 FIX: validate status_ptr mapping BEFORE the wait loop.
     * Old code only checked valid_user_ptr (range), not mapping. If
     * the user passed an unmapped pointer, the kernel would #PF when
     * writing the exit code, killing the kernel (not the user). Now
     * we check up-front and reject unmapped pointers. */
    if (status_ptr != 0 && !access_ok_read(status_ptr, sizeof(int)))
        return (u64)-1;
    tid_t parent_tid = core_kthread_current_tid();
    for (;;) {
        int found_alive = 0;
        for (int i = 0; i < MAX_USER_PROCS; i++) {
            if (g_procs[i].parent_tid != parent_tid) continue;
            if (g_procs[i].pid == 0) continue;
            if ((i64)pid_arg > 0 && g_procs[i].pid != (pid_t)pid_arg) continue;
            if (!g_procs[i].alive && !g_procs[i].waited) {
                g_procs[i].waited = 1;
                if (status_ptr)
                    *(int*)(uintptr_t)status_ptr = g_procs[i].exit_code;
                /* BUG-0099 FIX (A2-6): stale-tid guard. The child EXITED,
                 * the scheduler may already have reaped its task slot
                 * (core_sched_reap_exited) and a NEW task may have been
                 * created with the same tid. Blindly calling
                 * core_kthread_destroy(g_procs[i].tid) then killed the
                 * innocent new tenant of that slot (its stack freed under
                 * it mid-run). Only destroy when the slot still holds an
                 * EXITED task; if it was already reaped or reused, there
                 * is nothing left to destroy. */
                task_t *t = core_kthread_get_task(g_procs[i].tid);
                if (t && t->state == TASK_EXITED)
                    core_kthread_destroy(g_procs[i].tid);
                return (u64)g_procs[i].pid;
            }
            if (g_procs[i].alive) found_alive = 1;
        }
        if (!found_alive) return (u64)-1;
        core_kthread_block();
    }
}

static u64 sys_kill(u64 pid_arg, u64 sig, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    if (sig >= NSIG) return (u64)-1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].pid == (pid_t)pid_arg) {
            /* BUG-0277 FIX follow-up: resolve self-kill BEFORE the reap
             * clears g_procs[i].alive - user_process_current() only
             * returns LIVE entries, so a post-reap comparison always
             * misses and the kill returns into a destroyed AS. */
            int is_self = (user_process_current() == &g_procs[i]);
            if (sig == SIGKILL) {
                /* P3-11 FIX: Old code just set alive=0 + TASK_EXITED,
                 * leaking the entire PML4/PDPT/PD0/PT (~3-4 pages per
                 * kill) AND leaving any open pipe fds dangling (the
                 * reader_count/writer_count never decremented, so a
                 * blocked peer would block forever). Now we call the
                 * unified reaper — same path as sys_exit2. */
                user_process_reap_resources(&g_procs[i], 128 + (int)sig);
                if (g_procs[i].parent_tid > 0) core_kthread_wake(g_procs[i].parent_tid);
                /* WP-09-FIX BUG-004: remove from ready queue too (see
                 * core_sched_task_exited). */
                core_sched_task_exited(g_procs[i].tid);
                if (is_self) {
                    task_t *t = core_kthread_current();
                    if (t) t->state = TASK_EXITED;
                    for (;;) core_kthread_block();
                }
                return 0;
            }
            g_procs[i].pending_signal = (int)sig;
            if (g_procs[i].sig_handlers[sig] == NULL) {
                /* Default action terminates the process — use reaper. */
                user_process_reap_resources(&g_procs[i], 128 + (int)sig);
                if (g_procs[i].parent_tid > 0) core_kthread_wake(g_procs[i].parent_tid);
                core_sched_task_exited(g_procs[i].tid);
                if (is_self) {
                    /* Same self-kill rule as the SIGKILL path above:
                     * default action on the CALLING task (raise() to
                     * self with no handler) must never return to user
                     * mode — see the BUG-0277 follow-up comment. */
                    task_t *t = core_kthread_current();
                    if (t) t->state = TASK_EXITED;
                    for (;;) core_kthread_block();
                }
            }
            return 0;
        }
    }
    return (u64)-1;
}

static u64 sys_pipe(u64 pipefd_ptr, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    /* P3-9 FIX: validate pipefd_ptr is mapped (was only range-check). */
    if (!access_ok_read(pipefd_ptr, sizeof(int) * 2)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    int pipe_id = pipe_alloc();
    if (pipe_id == 0) return (u64)-1;
    int read_fd = sys_proc_fd_alloc(proc);
    if (read_fd < 0) { g_pipes[pipe_id-1].in_use = 0; return (u64)-1; }
    proc->fds[read_fd].kind = 2;
    proc->fds[read_fd].pipe_id = pipe_id;
    int write_fd = sys_proc_fd_alloc(proc);
    if (write_fd < 0) { proc->fds[read_fd].kind = 0; g_pipes[pipe_id-1].in_use = 0; return (u64)-1; }
    proc->fds[write_fd].kind = 3;
    proc->fds[write_fd].pipe_id = pipe_id;
    sys_pipe_t *p = pipe_get(pipe_id);
    if (p) { p->reader_count = 1; p->writer_count = 1; }
    int *pipefd = (int*)(uintptr_t)pipefd_ptr;
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    return 0;
}

static u64 sys_read(u64 fd, u64 buf, u64 count, u64 a4) {
    (void)a4;
    /* P3-9 FIX: verify buffer is mapped (was only range-check). */
    if (!access_ok_read(buf, count)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    sys_proc_fd_t *pfd = &proc->fds[fd];
    /* BUG-0090 FIX completion (A16-3): stdin (fd 0) is the console
     * keyboard. POSIX stdio semantics: read(0,...) blocks until at
     * least one byte is available from the keyboard queue, then drains
     * the queue (up to count). Before this, kind-0 reads returned -1,
     * so a stdin-reading builtin (bare `cat`) exited immediately after
     * a `cat < file` redirect instead of waiting on the keyboard. */
    if (pfd->kind == 0) {
        u8 *ub = (u8*)(uintptr_t)buf;
        u64 got = 0;
        for (;;) {
            /* sti;hlt pair (same pattern as sys_readline): the syscall
             * entry cleared IF, and without re-enabling interrupts the
             * keyboard IRQ can never fill the queue — the read would
             * spin forever on an empty buffer. hlt sleeps until the
             * IRQ arrives instead of burning the CPU. */
            __asm__ volatile("sti");
            __asm__ volatile("hlt");
            int k = driver_input_keyboard_getch();
            if (k < 0) continue;
            if (k == 0x04) {           /* Ctrl-D = EOF, POSIX tty rule */
                if (got == 0) return 0;
                break;
            }
            if (k == '\r') k = '\n';   /* Enter arrives as CR from the PS/2 layer */
            ub[got++] = (u8)k;
            if (k == '\n' || got >= count) break;
        }
        return got;
    }
    if (pfd->kind == 1) return (u64)fs_vfs_read(pfd->fs_vfs_fd, (void*)(uintptr_t)buf, (int)count);
    if (pfd->kind == 2) {
        sys_pipe_t *p = pipe_get(pfd->pipe_id);
        if (!p) return (u64)-1;
        /* BUG-0100 FIX (A2-7): lost-wakeup race. The old code checked
         * pipe_data_avail() OUTSIDE any critical section, then added
         * itself to the wait queue and blocked. A timer preemption (or
         * simply the writer running on the next slice) between the
         * check and the enqueue let the writer fill the pipe and call
         * pipe_wake_one() while reader_waiters was still empty: the
         * wake was lost, we then enqueued and blocked forever with
         * data already sitting in the buffer. Check + enqueue + block
         * are now one cli-atomic window; the writer can only run
         * before (its wake is then harmless, the recheck sees data)
         * or after (its wake finds us queued). core_kthread_block()
         * saves our (IF=0) rflags at the switch-out, so after wake we
         * re-enable IF explicitly before touching shared state again. */
        u64 irq_flags;
        for (;;) {
            __asm__ volatile("pushfq; popq %0; cli" : "=r"(irq_flags));
            if (pipe_data_avail(p) > 0) {
                if (irq_flags & 0x200) __asm__ volatile("sti");
                break;
            }
            if (p->writer_count == 0) {
                if (irq_flags & 0x200) __asm__ volatile("sti");
                return 0;  /* EOF: all writers gone */
            }
            /* P3-15: add to wait queue (FIFO), not single slot. */
            int my_tid = core_kthread_current_tid();
            pipe_wait_add(p->reader_waiters, my_tid);
            core_kthread_block();
            pipe_wait_remove(p->reader_waiters, my_tid);
            if (irq_flags & 0x200) __asm__ volatile("sti");
        }
        u32 avail = pipe_data_avail(p);
        if (avail > count) avail = (u32)count;
        u8 *dst = (u8*)(uintptr_t)buf;
        for (u32 i = 0; i < avail; i++)
            dst[i] = p->buf[(p->read_pos + i) % PIPE_BUF_SIZE];
        p->read_pos += avail;
        /* P3-15: wake ONE writer (not just one specific tid). */
        pipe_wake_one(p->writer_waiters);
        return (u64)avail;
    }
    return (u64)-1;
}

static u64 sys_write(u64 fd, u64 buf, u64 len, u64 a4) {
    (void)a4;
    /* P3-9 FIX: verify buffer is mapped (was only range-check).
     * If no current proc (early debug path), still need range-check only. */
    if (!valid_user_buf(buf, len)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) {
        const char *p = (const char*)(uintptr_t)buf;
        for (u64 i = 0; i < len; i++) screen_console_putc(p[i]);
        return len;
    }
    /* P3-9: for the actual write path, mapping is required. */
    if (!access_ok_read(buf, len)) return (u64)-1;
    /* P2-10 FIX: do NOT hardcode fd 1/2 to console. A user process can
     * redirect stdout/stderr via dup2 (e.g. `cmd > file` runs in ush:
     * sys_dup2(fs_vfs_fd, 1)). The old code unconditionally wrote to the
     * console for fd==1 or fd==2, bypassing the fd table and breaking
     * output redirection. Now we consult the fd table first, and only
     * fall back to the console if the fd slot is genuinely empty. */
    if (fd < PROC_MAX_FDS) {
        sys_proc_fd_t *pfd = &proc->fds[fd];
        if (pfd->kind == 1) {
            return (u64)fs_vfs_write(pfd->fs_vfs_fd, (const void*)(uintptr_t)buf, (int)len);
        }
        if (pfd->kind == 3) {
            sys_pipe_t *p = pipe_get(pfd->pipe_id);
            if (!p) return (u64)-1;
            u32 remaining = (u32)len;
            u64 total_written = 0;
            const u8 *src = (const u8*)(uintptr_t)buf;
            u64 irq_flags;
            while (remaining > 0) {
                /* BUG-0100 FIX (A2-7): writer-side lost-wakeup race,
                 * mirror of the sys_read fix below/above: the old code
                 * checked pipe_space_avail() un-protected, then
                 * enqueued and blocked - a reader draining and waking
                 * in that window lost the wake and the writer slept
                 * with room already available. Check + enqueue + block
                 * are now one cli-atomic window. */
                __asm__ volatile("pushfq; popq %0; cli" : "=r"(irq_flags));
                while (pipe_space_avail(p) == 0) {
                    /* P6 fix: old code returned -1 when reader_count == 0,
                     * losing data. This caused the ush pipe race: the
                     * producer child (echo) would write to the pipe while
                     * the consumer child (cat) hadn't been scheduled yet.
                     * If reader_count was 0 (race with close), the write
                     * failed and the data was lost forever.
                     *
                     * Fix: only fail if the buffer is full AND reader_count
                     * is 0 (no one will ever read the data). If there's
                     * space in the buffer, write the data even with 0
                     * readers — a future reader (forked later by the
                     * parent) will find it. */
                    if (p->reader_count == 0) {
                        /* Buffer is full and no readers. In POSIX this
                         * would be SIGPIPE/EPIPE. We return what we've
                         * written so far (or -1 if nothing). */
                        if (irq_flags & 0x200) __asm__ volatile("sti");
                        return total_written > 0 ? total_written : (u64)-1;
                    }
                    /* P3-15: wait queue (FIFO), not single slot. */
                    int my_tid = core_kthread_current_tid();
                    pipe_wait_add(p->writer_waiters, my_tid);
                    core_kthread_block();
                    pipe_wait_remove(p->writer_waiters, my_tid);
                    /* BUG-0100 FIX: woken inside the cli window - the
                     * block saved IF=0 (BUG-0103 restores it exactly),
                     * re-enable before the space recheck loops. */
                    if (irq_flags & 0x200) __asm__ volatile("sti");
                }
                /* Space is available (or the first pass fell straight
                 * through the loop): leave the atomic check window
                 * before the data copy so we never keep interrupts off
                 * across the copy / wake path or return with IF=0. */
                if (irq_flags & 0x200) __asm__ volatile("sti");
                u32 space = pipe_space_avail(p);
                u32 to_write = remaining < space ? remaining : space;
                for (u32 i = 0; i < to_write; i++)
                    p->buf[(p->write_pos + i) % PIPE_BUF_SIZE] = src[total_written + i];
                p->write_pos += to_write;
                total_written += to_write;
                remaining -= to_write;
                /* P3-15: wake ONE reader (FIFO). */
                pipe_wake_one(p->reader_waiters);
            }
            return total_written;
        }
    }
    /* fd not pointing to a real file/pipe. For the conventional
     * "stdout/stderr" fds 1 and 2, fall back to the console.
     * WP-09-FIX BUG-023: emit the whole write atomically (cli window,
     * capped at 512 bytes per chunk) so concurrent user processes cannot
     * interleave character-by-character on the serial console
     * ("parent: pid= 3 ppid=0 child=4child: pid= 4 ppid=3"). */
    if (fd == 1 || fd == 2) {
        const char *p = (const char*)(uintptr_t)buf;
        u64 done = 0;
        while (done < len) {
            u64 chunk = len - done;
            if (chunk > 512) chunk = 512;
            u64 eflags;
            __asm__ volatile("pushfq; popq %0; cli" : "=r"(eflags));
            for (u64 i = 0; i < chunk; i++) screen_console_putc(p[done + i]);
            __asm__ volatile("pushq %0; popfq" : : "r"(eflags));
            done += chunk;
        }
        return len;
    }
    return (u64)-1;
}

static u64 sys_dup(u64 fd, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    sys_proc_fd_t *pfd = &proc->fds[fd];
    int new_fd = sys_proc_fd_alloc(proc);
    if (new_fd < 0) return (u64)-1;
    /* BUG-0090 FIX completion (A16-3): fd 0/1/2 start as kind-0 console
     * placeholders, but POSIX shells MUST be able to dup/dup2 them to
     * save and restore stdio across a redirect (ush does exactly that).
     * A kind-0 dup just clones the placeholder: no pipe refcount, no VFS
     * refcount, nothing to release on close. Returning -1 here made every
     * ush redirect PERMANENTLY re-bind fd 1 to the redirect file (the
     * save failed, so the restore was skipped) — console output of the
     * whole session then went into that file. */
    if (pfd->kind == 0) {
        memset(&proc->fds[new_fd], 0, sizeof(proc->fds[new_fd]));
        proc->fds[new_fd].kind = 0;
        return (u64)new_fd;
    }
    proc->fds[new_fd] = *pfd;
    if (pfd->kind == 2) { sys_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->reader_count++; }
    else if (pfd->kind == 3) { sys_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->writer_count++; }
    else if (pfd->kind == 1) {
        /* BUG-0098 FIX (A2-5): dup shares the open file - add a
         * reference so the two fds close independently. */
        fs_vfs_fd_addref(pfd->fs_vfs_fd);
    }
    return (u64)new_fd;
}

static u64 sys_dup2(u64 oldfd, u64 newfd, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (oldfd >= PROC_MAX_FDS || newfd >= PROC_MAX_FDS) return (u64)-1;
    sys_proc_fd_t *pfd = &proc->fds[oldfd];
    /* BUG-0090 FIX completion (A16-3): kind-0 (console) sources are
     * valid dup2 sources — restoring stdio after a redirect dup2's a
     * saved kind-0 console placeholder back over the redirected slot. */
    if (pfd->kind == 0) {
        if (oldfd == newfd) return (u64)newfd;
        if (proc->fds[newfd].kind != 0) {
            if (proc->fds[newfd].kind == 2) { sys_pipe_t *p = pipe_get(proc->fds[newfd].pipe_id); if (p) p->reader_count--; }
            else if (proc->fds[newfd].kind == 3) { sys_pipe_t *p = pipe_get(proc->fds[newfd].pipe_id); if (p) p->writer_count--; }
            else if (proc->fds[newfd].kind == 1) {
                fs_vfs_close(proc->fds[newfd].fs_vfs_fd);
            }
        }
        memset(&proc->fds[newfd], 0, sizeof(proc->fds[newfd]));
        proc->fds[newfd].kind = 0;
        return (u64)newfd;
    }
    if (oldfd == newfd) return (u64)newfd;
    if (proc->fds[newfd].kind != 0) {
        if (proc->fds[newfd].kind == 2) { sys_pipe_t *p = pipe_get(proc->fds[newfd].pipe_id); if (p) p->reader_count--; }
        else if (proc->fds[newfd].kind == 3) { sys_pipe_t *p = pipe_get(proc->fds[newfd].pipe_id); if (p) p->writer_count--; }
        else if (proc->fds[newfd].kind == 1) {
            /* BUG-0098 FIX (A2-5): the overwritten target's reference to
             * its open file goes away with the overwrite - release it via
             * fs_vfs_close, which now only drops the reference (the slot
             * itself survives while any other holder keeps one). The old
             * code leaked the slot outright here. */
            fs_vfs_close(proc->fds[newfd].fs_vfs_fd);
        }
        proc->fds[newfd].kind = 0;
    }
    proc->fds[newfd] = *pfd;
    if (pfd->kind == 2) { sys_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->reader_count++; }
    else if (pfd->kind == 3) { sys_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->writer_count++; }
    else if (pfd->kind == 1) {
        /* BUG-0098 FIX (A2-5): dup2 shares the open file - add a
         * reference so oldfd and newfd close independently. */
        fs_vfs_fd_addref(pfd->fs_vfs_fd);
    }
    return (u64)newfd;
}

/* BUG-0281 (A2-9) FIX: mmap window region-table helpers. Regions are
 * non-overlapping by construction (mmap refuses overlaps), so coverage
 * and overlap tests reduce to interval arithmetic over the table. All
 * of these run on the CALLER'S OWN process table - "foreign" here means
 * a window range this process never mapped, which is exactly what the
 * teardown calls must refuse instead of silently ignoring. */
static int mmap_regions_overlaps(user_proc_t *p, u64 s, u64 e) {
    for (int i = 0; i < PROC_MAX_MMAP_REGIONS; i++) {
        if (p->mmap_regions[i].start == 0) continue;
        u64 rs = p->mmap_regions[i].start;
        u64 re = rs + p->mmap_regions[i].pages * 0x1000;
        if (s < re && rs < e) return 1;
    }
    return 0;
}

/* 1 = every page of [s, e) lies inside some region of p. */
static int mmap_regions_covered(user_proc_t *p, u64 s, u64 e) {
    u64 covered = 0;
    for (int i = 0; i < PROC_MAX_MMAP_REGIONS; i++) {
        if (p->mmap_regions[i].start == 0) continue;
        u64 rs = p->mmap_regions[i].start;
        u64 re = rs + p->mmap_regions[i].pages * 0x1000;
        u64 os = s > rs ? s : rs;
        u64 oe = e < re ? e : re;
        if (oe > os) covered += oe - os;
    }
    return covered == e - s;
}

/* Carve [s, e) out of the region table: edge overlap trims a region,
 * an interior range splits it into two survivors. Returns -1 (without
 * touching anything) when a split would need a free slot and the table
 * is full, so callers can reject BEFORE unmapping any PTE. */
static int mmap_regions_carve(user_proc_t *p, u64 s, u64 e) {
    int splits_needed = 0;
    for (int i = 0; i < PROC_MAX_MMAP_REGIONS; i++) {
        if (p->mmap_regions[i].start == 0) continue;
        u64 rs = p->mmap_regions[i].start;
        u64 re = rs + p->mmap_regions[i].pages * 0x1000;
        if (rs < s && e < re) splits_needed++;  /* interior: 2 survivors */
    }
    int free_slots = 0;
    for (int i = 0; i < PROC_MAX_MMAP_REGIONS; i++)
        if (p->mmap_regions[i].start == 0) free_slots++;
    if (splits_needed > free_slots) return -1;

    for (int i = 0; i < PROC_MAX_MMAP_REGIONS; i++) {
        if (p->mmap_regions[i].start == 0) continue;
        u64 rs = p->mmap_regions[i].start;
        u64 re = rs + p->mmap_regions[i].pages * 0x1000;
        if (re <= s || rs >= e) continue;         /* untouched */
        if (rs >= s && re <= e) {                  /* fully removed */
            p->mmap_regions[i].start = 0;
            p->mmap_regions[i].pages = 0;
            continue;
        }
        if (rs < s) {                              /* keep left survivor */
            p->mmap_regions[i].pages = (s - rs) / 0x1000;
            if (e < re) {                          /* + right survivor */
                for (int j = 0; j < PROC_MAX_MMAP_REGIONS; j++) {
                    if (p->mmap_regions[j].start != 0) continue;
                    p->mmap_regions[j].start = e;
                    p->mmap_regions[j].pages = (re - e) / 0x1000;
                    break;
                }
            }
        } else {                                   /* keep right survivor */
            u64 npages = (re - e) / 0x1000;
            p->mmap_regions[i].start = e;
            p->mmap_regions[i].pages = npages;
        }
    }
    return 0;
}

static u64 sys_mem_mmap(u64 addr, u64 length, u64 prot, u64 a4) {
    /* BUG-0281 (A2-9) FIX: the old implementation threw away addr and
     * prot ((void)addr;(void)prot;) and always mapped PRESENT|WRITE|USER:
     * MAP_FIXED, PROT_NONE and read-only were silently upgraded to RW.
     * Now:
     *   - prot is honoured and validated (bits outside PROT_READ|WRITE|
     *     EXEC are rejected; PROT_NONE maps the frames PRESENT but
     *     WITHOUT the USER bit, so ring 3 faults on any access while
     *     the PTE stays reclaimable by munmap/mprotect);
     *   - a non-zero addr is a real hint: page-aligned and inside the
     *     window, or the call is rejected - never silently redirected;
     *   - the allocation is bounded by the SAME window end that munmap
     *     and mprotect enforce (USER_MMAP_WINDOW_END, just below the
     *     user stack), so a mapping can no longer grow past the reach
     *     of its own teardown syscalls;
     *   - the region is recorded in proc->mmap_regions, which is what
     *     munmap/mprotect use to verify ownership (a fork child keeps
     *     the copied pages but restarts with an empty table, matching
     *     sys_fork's documented non-inheritance of mmap_base - A2-10
     *     domain).
     * a4 (flags) is accepted-and-identical by design: this kernel has a
     * single anonymous mapping type, MAP_SHARED/MAP_PRIVATE have the
     * same semantics here (documented; no other flag bits exist). */
    (void)a4;
    if (length == 0) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    /* P3-6 FIX: sanity cap (unchanged); the window bound below is the
     * real limit. */
    if (length > 0x40000000ULL) return (u64)-1;  /* 1 GiB sanity cap */
    if (prot & ~7ULL) return (u64)-1;  /* PROT_* mask is 0..7 */
    u64 pages = (length + 0xFFF) / 0x1000;
    u64 map_flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    if (prot & 2) map_flags |= VMM_FLAG_WRITE;
    if (!(prot & 4)) map_flags |= VMM_FLAG_NOEXEC;
    if (!(prot & 1)) map_flags &= ~VMM_FLAG_USER;  /* PROT_NONE */
    u64 base;
    if (addr == 0) {
        /* BUG-010 FIX: per-process bump allocator (see original note). */
        if (proc->mmap_base == 0) proc->mmap_base = USER_MMAP_BASE;
        base = proc->mmap_base;
    } else {
        /* BUG-0281: addr is a real hint now - page-aligned and inside
         * the mmap window, or rejected. */
        if (addr & 0xFFF) return (u64)-1;
        if (addr < USER_MMAP_BASE) return (u64)-1;
        base = addr;
    }
    /* BUG-0281: the whole [base, base+span) range must sit inside the
     * ONE window [USER_MMAP_BASE, USER_MMAP_WINDOW_END) that munmap and
     * mprotect enforce - a mapping above the window end would be
     * unreachable by its own teardown syscalls (the exact A2-9
     * inconsistency). The base>END case must be tested explicitly:
     * USER_MMAP_WINDOW_END - base would wrap around in unsigned
     * arithmetic and silently pass the pages check. base == END is
     * rejected by the pages check (any length needs >= 1 page). */
    if (base < USER_MMAP_BASE ||
        base > USER_MMAP_WINDOW_END ||
        pages > (USER_MMAP_WINDOW_END - base) / 0x1000) return (u64)-1;
    u64 span = pages * 0x1000;
    /* Refuse to overlap a region this process already owns (a silent
     * overwrite would leak the old frames and replace live data), and
     * refuse when the region table is full - both checks happen BEFORE
     * any frame is allocated. */
    if (mmap_regions_overlaps(proc, base, base + span)) return (u64)-1;
    int slot = -1;
    for (int i = 0; i < PROC_MAX_MMAP_REGIONS; i++) {
        if (proc->mmap_regions[i].start == 0) { slot = i; break; }
    }
    if (slot < 0) return (u64)-1;
    u64 result = base;
    u64 mapped = 0;  /* P3-5: track how many we successfully mapped */
    for (u64 i = 0; i < pages; i++) {
        u64 phys = mem_pmm_alloc_frame();
        if (phys == 0) {
            /* P3-5 FIX: Partial-failure cleanup (unchanged). */
            for (u64 j = 0; j < mapped; j++) {
                u64 old_pte = mem_vmm_unmap_page(proc->as, base + j * 0x1000);
                if (old_pte & VMM_FLAG_PRESENT) {
                    u64 p = old_pte & 0x000FFFFFFFFFF000ULL;
                    if (p != 0) mem_pmm_free_frame(p);
                }
            }
            return (u64)-1;
        }
        mem_vmm_map_page(proc->as, base + i * 0x1000, phys, map_flags);
        memset((void*)phys, 0, 0x1000);
        mapped++;
    }
    proc->mmap_regions[slot].start = base;
    proc->mmap_regions[slot].pages = pages;
    /* Bump only moves forward: past anonymous allocations, and past a
     * successful hint range so a later bump can never collide with it. */
    if (addr == 0 || base + span > proc->mmap_base)
        proc->mmap_base = base + span;
    return result;
}

/* BUG-003 FIX (P0): Validate that the target address range lies within
 * user-accessible virtual memory (USER_BRK_BASE .. USER_MMAP_BASE+LIMIT)
 * BEFORE modifying PTEs. Without this check, a malicious or buggy user
 * program could call sys_mem_munmap on a kernel address (e.g., 0x100000)
 * and unmap kernel pages — the kernel would then triple-fault on the
 * next access to that address, or worse, the freed frames could be
 * reused by a subsequent mmap, giving the user program read/write
 * access to kernel physical memory (privilege escalation).
 *
 * The same risk exists for sys_mem_mprotect — modifying protection bits on
 * a kernel page could grant user write access to kernel data.
 *
 * Fix: reject any address outside the user-mapped region. */
static u64 sys_mem_munmap(u64 addr, u64 length, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    /* P3-6 FIX: page-align addr and length, and check overflow.
     * addr is rounded DOWN to a page boundary; length is rounded UP.
     * addr + length must not wrap (overflow). */
    if (length == 0) return (u64)-1;
    if (length > 0x40000000ULL) return (u64)-1;
    u64 end = addr + length;
    if (end < addr) return (u64)-1;  /* overflow */
    addr = addr & ~0xFFFULL;
    u64 end_aligned = (end + 0xFFF) & ~0xFFFULL;
    /* BUG-003 FIX (P0): reject addresses outside user-mapped region.
     * User mappings live in [USER_BRK_BASE, USER_MMAP_WINDOW_END).
     * Anything outside this range is either kernel identity-mapped
     * (0..USER_BRK_BASE) or the user stack / shared kernel page-table
     * region. We refuse to munmap kernel pages. */
    if (addr < USER_BRK_BASE) return (u64)-1;
    /* BUG-0281: SAME window bound mmap enforces (was the unreachable
     * USER_MMAP_BASE+0x10000000, which also covered the user stack).
     * Anything mmap can produce is now munmap-able, and nothing above
     * the window (e.g. the stack) is not. */
    if (end_aligned > USER_MMAP_WINDOW_END) return (u64)-1;
    u64 pages = (end_aligned - addr) / 0x1000;
    /* BUG-0281: the window portion of the range must belong to regions
     * this process actually mapped (foreign/unmapped window ranges are
     * rejected instead of silently "succeeding"). The carve also needs
     * a slot check up front so a split can never leave a half-updated
     * table behind a failed munmap. Below-window ranges (brk, solib)
     * keep the historical per-page teardown behaviour. */
    u64 ws = addr > USER_MMAP_BASE ? addr : USER_MMAP_BASE;
    u64 we = end_aligned < USER_MMAP_WINDOW_END ? end_aligned : USER_MMAP_WINDOW_END;
    if (ws < we) {
        if (!mmap_regions_covered(proc, ws, we)) return (u64)-1;
        if (mmap_regions_carve(proc, ws, we) != 0) return (u64)-1;
    }
    /* P2-26 FIX: Free the physical pages backing the unmapped VA range.
     * mem_vmm_unmap_page returns the old PTE, from which we extract the
     * physical frame and return it to PMM. Without this, munmap leaks
     * physical memory (the VA is unmapped but the frame remains
     * allocated). */
    /* P3-3 FIX: mem_vmm_unmap_page now returns the FULL old PTE (address +
     * flags), so the PRESENT-bit check below actually works (old code
     * masked off flags so PRESENT was always 0 → never freed). */
    for (u64 i = 0; i < pages; i++) {
        u64 old_pte = mem_vmm_unmap_page(proc->as, addr + i * 0x1000);
        if (old_pte & VMM_FLAG_PRESENT) {
            u64 phys = old_pte & 0x000FFFFFFFFFF000ULL;
            if (phys != 0) mem_pmm_free_frame(phys);
        }
    }
    return 0;
}

static u64 sys_mem_mprotect(u64 addr, u64 len, u64 prot, u64 a4) {
    (void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    /* P3-6 FIX: page-align and overflow-check, same as sys_mem_munmap. */
    if (len == 0) return (u64)-1;
    if (len > 0x40000000ULL) return (u64)-1;
    u64 end = addr + len;
    if (end < addr) return (u64)-1;
    addr = addr & ~0xFFFULL;
    u64 end_aligned = (end + 0xFFF) & ~0xFFFULL;
    /* BUG-003 FIX (P0): reject addresses outside user-mapped region.
     * Same rationale as sys_mem_munmap — don't let user code modify
     * protection bits on kernel pages. */
    if (addr < USER_BRK_BASE) return (u64)-1;
    /* BUG-0281: SAME window bound mmap/munmap enforce (the old
     * USER_MMAP_BASE+0x10000000 also covered the user stack). */
    if (end_aligned > USER_MMAP_WINDOW_END) return (u64)-1;
    /* BUG-0281: prot must be a valid PROT_* mask. The old code silently
     * dropped unknown bits; PROT_NONE (0) maps the pages PRESENT but
     * WITHOUT the USER bit (ring 3 faults on any access) so the PTEs
     * stay reclaimable by munmap and re-protectable by mprotect. */
    if (prot & ~7ULL) return (u64)-1;
    u64 flags = VMM_FLAG_PRESENT;
    if (prot & 1) flags |= VMM_FLAG_USER;
    if (prot & 2) flags |= VMM_FLAG_WRITE;
    if (!(prot & 4)) flags |= VMM_FLAG_NOEXEC;
    u64 pages = (end_aligned - addr) / 0x1000;
    /* BUG-0281: the window portion of the range must belong to regions
     * this process actually mapped - mprotect on someone else's (or an
     * unmapped) window range is rejected instead of silently ignored.
     * Below-window ranges (brk, loader/solib text+data for the W^X
     * RELRO pass) keep the historical behaviour. */
    u64 ws = addr > USER_MMAP_BASE ? addr : USER_MMAP_BASE;
    u64 we = end_aligned < USER_MMAP_WINDOW_END ? end_aligned : USER_MMAP_WINDOW_END;
    if (ws < we && !mmap_regions_covered(proc, ws, we)) return (u64)-1;
    for (u64 i = 0; i < pages; i++) mem_vmm_protect_page(proc->as, addr + i * 0x1000, flags);
    return 0;
}

static u64 sys_mem_brk(u64 addr, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (addr == 0) return proc->brk;
    if (addr < USER_BRK_BASE) return proc->brk;
    if (addr >= USER_MMAP_BASE) return proc->brk;
    u64 old_brk = proc->brk;
    u64 new_brk = (addr + 0xFFF) & ~0xFFFULL;
    if (new_brk > old_brk) {
        /* Expand: map new pages from old_brk..new_brk. */
        u64 vaddr = (old_brk + 0xFFF) & ~0xFFFULL;
        u64 mapped_since_start = 0;  /* P3-8: for OOM rollback */
        while (vaddr < new_brk) {
            u64 phys = mem_pmm_alloc_frame();
            if (phys == 0) {
                /* P3-8 FIX: OOM during expansion — old code returned
                 * proc->brk (the OLD brk) but left the partial
                 * mappings in place. The user would see brk unchanged
                 * but the physical pages were allocated (leaked).
                 * Now we unmap and free everything we mapped in this
                 * call before returning the old brk. */
                u64 rollback_vaddr = (old_brk + 0xFFF) & ~0xFFFULL;
                for (u64 j = 0; j < mapped_since_start; j++) {
                    u64 old_pte = mem_vmm_unmap_page(proc->as, rollback_vaddr + j * 0x1000);
                    if (old_pte & VMM_FLAG_PRESENT) {
                        u64 p = old_pte & 0x000FFFFFFFFFF000ULL;
                        if (p != 0) mem_pmm_free_frame(p);
                    }
                }
                return proc->brk;  /* brk unchanged */
            }
            mem_vmm_map_page(proc->as, vaddr, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            memset((void*)phys, 0, 0x1000);
            vaddr += 0x1000;
            mapped_since_start++;
        }
    } else if (new_brk < old_brk) {
        /* P3-7 FIX: Shrink — unmap and free pages in [new_brk, old_brk).
         * Old code silently updated proc->brk without releasing the
         * backing physical pages, leaking one frame per page of brk
         * shrink. Common pattern: malloc(N) then free(N) → user calls
         * sbrk(-N) → pages were never reclaimed. */
        u64 vaddr = new_brk;
        while (vaddr < old_brk) {
            u64 old_pte = mem_vmm_unmap_page(proc->as, vaddr);
            if (old_pte & VMM_FLAG_PRESENT) {
                u64 p = old_pte & 0x000FFFFFFFFFF000ULL;
                if (p != 0) mem_pmm_free_frame(p);
            }
            vaddr += 0x1000;
        }
    }
    proc->brk = new_brk;
    return proc->brk;
}

static u64 sys_signal(u64 sig, u64 handler, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    if (sig >= NSIG) return (u64)-1;
    user_proc_t *p = user_process_current();
    if (!p) return (u64)-1;
    signal_handler_fn old = p->sig_handlers[sig];
    p->sig_handlers[sig] = (signal_handler_fn)handler;
    return (u64)old;
}

static u64 sys_sigaction(u64 sig, u64 act_ptr, u64 oldact_ptr, u64 a4) {
    (void)a4;
    if (sig >= NSIG) return (u64)-1;
    user_proc_t *p = user_process_current();
    if (!p) return (u64)-1;
    signal_handler_fn old = p->sig_handlers[sig];
    /* P3-9 FIX: verify act_ptr/oldact_ptr mappings (was only range). */
    if (act_ptr && access_ok_read(act_ptr, sizeof(sigaction_t))) {
        sigaction_t *act = (sigaction_t*)(uintptr_t)act_ptr;
        p->sig_handlers[sig] = act->sa_handler;
    }
    if (oldact_ptr && access_ok_read(oldact_ptr, sizeof(sigaction_t))) {
        sigaction_t *oldact = (sigaction_t*)(uintptr_t)oldact_ptr;
        oldact->sa_handler = old;
        oldact->sa_flags = 0;
        oldact->sa_mask = 0;
    }
    return 0;
}

static u64 sys_sigreturn(u64 a1, u64 a2, u64 a3, u64 a4) {
    /* P3-12 FIX: Real sigreturn implementation.
     *
     * When the kernel delivers a signal (in core_syscall_dispatch), it
     * saves the pre-signal IRQ frame into proc->sig_saved_frame
     * (22 u64s: r15..ss) and modifies the live frame so iretq jumps
     * to the user's signal handler.
     *
     * The handler runs in user mode. When it's done, it invokes
     * SYS_SIGRETURN (int 0x80 with rax=42). At that point we are
     * back in core_syscall_dispatch with the live frame pointing at the
     * instruction AFTER the handler's `int 0x80`. We:
     *   1. Find the current proc.
     *   2. Check sig_in_progress — if 0, the call is bogus (return 0).
     *   3. Copy sig_saved_frame back into the live IRQ frame
     *      (which is g_current_frame / the regs[] passed to
     *      core_syscall_dispatch). This overwrites rip, rsp, rflags,
     *      rdi, etc. — restoring the user's pre-signal state.
     *   4. Clear sig_in_progress.
     *
     * After sys_sigreturn returns, core_syscall_dispatch continues and
     * checks pending_signal again — but we've cleared it during
     * delivery, so no infinite recursion. iretq then jumps to the
     * original RIP with the original RSP/RFLAGS/registers.
     *
     * Restored state includes:
     *   - All GP registers (r15..rax)
     *   - RIP, RSP, RFLAGS
     *   - CS, SS (segment selectors — ring-3 user-mode)
     *   - Signal mask is implicit (no separate mask in this kernel)
     *
     * The signal mask is "implicitly" restored because there's no
     * explicit signal-mask field in this kernel's user_proc_t.
     * Pending signals during handler execution were never blocked
     * — a future hardening could add a sig_blocked field. */
    (void)a1;(void)a2;(void)a3;(void)a4;
    user_proc_t *p = user_process_current();
    if (!p || !p->sig_in_progress) return 0;  /* bogus sigreturn */
    /* Get the live frame pointer saved by core_syscall_dispatch. */
    u64 *frame = user_get_current_frame();
    if (!frame) return 0;
    /* Restore the 22 u64 frame. */
    for (int i = 0; i < 22; i++)
        frame[i] = p->sig_saved_frame[i];
    p->sig_in_progress = 0;
    return 0;
}

static u64 sys_chdir(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    /* BUG-0278 FIX (A2-14): chdir moves THIS process's cwd, not the
     * kernel shell's global g_cwd. The path is copy-in'd first (the old
     * code handed the raw user pointer to shell_set_cwd, which read it
     * directly - style-inconsistent with the rest of the syscall layer).
     * Validation mirrors the kernel shell's cd: the resolved path must
     * exist and be a directory. */
    if (!access_ok_str(path)) return (u64)-1;
    char user_buf[256];
    {
        const char *p = (const char*)(uintptr_t)path;
        u64 n = 0;
        while (n < 255 && p[n] != '\0') n++;
        if (copy_from_user(user_buf, path, n + 1) != 0) return (u64)-1;
        user_buf[n] = 0;
    }
    char resolved[USER_CWD_LEN];
    {
        extern int shell_resolve_path_base(const char *cwd, const char *path,
                                           char *out, int out_len);
        if (shell_resolve_path_base(proc->cwd, user_buf, resolved,
                                    (int)sizeof(resolved)) < 0)
            return (u64)-1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(resolved, &st) < 0) return (u64)-1;
    if (st.type != VFS_TYPE_DIR) return (u64)-1;
    /* resolved is <= USER_CWD_LEN-1 bytes (normalize enforces maxlen). */
    strcpy(proc->cwd, resolved);
    return 0;
}

static u64 sys_getcwd(u64 buf, u64 size, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    /* P3-9 FIX: caller wants us to write 'size' bytes (or fewer). Verify
     * the destination range is mapped before writing. We use copy_to_user
     * for the actual write so the access-check + copy is atomic-ish. */
    if (!access_ok_read(buf, size)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    /* BUG-0278 FIX (A2-14): report THIS process's cwd, not the kernel
     * shell's global g_cwd. */
    int len = strlen(proc->cwd);
    if (len >= (int)size) return (u64)-1;
    if (copy_to_user(buf, proc->cwd, (u64)len + 1) != 0) return (u64)-1;
    return buf;
}

static u64 sys_ioctl(u64 fd, u64 cmd, u64 arg, u64 a4) {
    (void)fd;(void)cmd;(void)arg;(void)a4;
    return 0;
}

#define FD_SET_BYTES 128

static u64 sys_select(u64 nfds, u64 readfds_ptr, u64 timeout_ms, u64 a4) {
    (void)a4;
    /* P3-9 FIX: verify readfds buffer is mapped (was only range-check). */
    if (!readfds_ptr || !access_ok_read(readfds_ptr, FD_SET_BYTES)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    u8 *readfds = (u8*)(uintptr_t)readfds_ptr;
    extern u64 core_timer_ticks(void);
    u64 start_ticks = core_timer_ticks();
    u64 timeout_ticks = timeout_ms / 10;
    if (timeout_ticks == 0 && timeout_ms > 0) timeout_ticks = 1;
    tid_t my_tid = core_kthread_current_tid();

    for (;;) {
        int ready_count = 0;
        int bad_fd = 0;
        u8 result[FD_SET_BYTES];
        memset(result, 0, FD_SET_BYTES);
        /* BUG-0100 FIX (A2-7) + BUG-0101 FIX (A2-8): the whole
         * scan -> timeout-check -> enqueue -> block sequence runs
         * inside ONE cli-atomic window. The old code re-opened the
         * lost-wakeup hole the pipe read/write fixes close: data
         * could arrive (and wake an empty wait queue) between the
         * ready scan and the pipe_wait_add, leaving the thread
         * asleep with a readable pipe. Single-CPU cli makes scan +
         * enqueue atomic against the writer; core_kthread_block()
         * saves our IF=0 rflags at switch-out (BUG-0103), so we
         * re-enable IF right after the wake. */
        u64 irq_flags;
        __asm__ volatile("pushfq; popq %0; cli" : "=r"(irq_flags));
        for (u64 fd = 0; fd < nfds && fd < PROC_MAX_FDS; fd++) {
            if (!(readfds[fd / 8] & (1 << (fd % 8)))) continue;
            sys_proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 0) {
                /* BUG-0101 FIX (A2-8): POSIX semantics. An fd that is
                 * not open is EBADF, not "never ready": the old code
                 * silently skipped it, so a select set containing only
                 * such fds (e.g. stdin 0, which this kernel does not
                 * auto-open) was neither ready nor waitable - with
                 * timeout_ms == 0 it blocked FOREVER instead of
                 * returning immediately. */
                bad_fd = 1;
                break;
            }
            int is_ready = 0;
            if (pfd->kind == 2) {
                sys_pipe_t *p = pipe_get(pfd->pipe_id);
                if (p && (pipe_data_avail(p) > 0 || p->writer_count == 0)) is_ready = 1;
            } else if (pfd->kind == 1) is_ready = 1;
            if (is_ready) { result[fd / 8] |= (1 << (fd % 8)); ready_count++; }
        }
        if (bad_fd) {
            if (irq_flags & 0x200) __asm__ volatile("sti");
            return (u64)-1;  /* EBADF */
        }
        if (ready_count > 0) {
            if (irq_flags & 0x200) __asm__ volatile("sti");
            memcpy(readfds, result, FD_SET_BYTES);
            return (u64)ready_count;
        }
        if (timeout_ms == 0) {
            /* BUG-0101 FIX (A2-8): timeout_ms == 0 means "poll once and
             * return immediately" (POSIX). The old code treated 0 as
             * "no timeout" and fell through to an unbounded block -
             * the exact opposite. */
            if (irq_flags & 0x200) __asm__ volatile("sti");
            memcpy(readfds, result, FD_SET_BYTES);
            return 0;
        }
        if (timeout_ms != (u64)-1) {
            u64 elapsed = core_timer_ticks() - start_ticks;
            if (elapsed >= timeout_ticks) {
                if (irq_flags & 0x200) __asm__ volatile("sti");
                memcpy(readfds, result, FD_SET_BYTES);
                return 0;
            }
        }
        for (u64 fd = 0; fd < nfds && fd < PROC_MAX_FDS; fd++) {
            if (!(readfds[fd / 8] & (1 << (fd % 8)))) continue;
            sys_proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 2) {
                sys_pipe_t *p = pipe_get(pfd->pipe_id);
                /* P3-15: add to wait queue (was single-slot). */
                if (p) pipe_wait_add(p->reader_waiters, my_tid);
            }
        }
        core_kthread_block();
        for (u64 fd = 0; fd < nfds && fd < PROC_MAX_FDS; fd++) {
            if (!(readfds[fd / 8] & (1 << (fd % 8)))) continue;
            sys_proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 2) {
                sys_pipe_t *p = pipe_get(pfd->pipe_id);
                /* P3-15: remove from wait queue. */
                if (p) pipe_wait_remove(p->reader_waiters, my_tid);
            }
        }
        if (irq_flags & 0x200) __asm__ volatile("sti");
    }
}

typedef struct { int fd; short events; short revents; } pollfd_t;

static u64 sys_poll(u64 fds_ptr, u64 nfds, u64 timeout_ms, u64 a4) {
    (void)a4;
    /* P0fix2 BUG-0031 (A2-1): nfds is user-controlled and the old
     * `nfds * sizeof(pollfd_t)` silently wrapped mod 2^64 (nfds = 2^61
     * multiplies to 0), so the access_ok check passed with len 0 and the
     * loop then wrote pfds[i].revents far beyond the validated range —
     * the first unmapped page made it a ring-0 #PF and a kernel HALT.
     * Cap nfds: fd indices are checked against PROC_MAX_FDS below, so any
     * larger poll can never be meaningful. */
    if (nfds > 4096) return (u64)-1;
    /* P3-9 FIX: verify pollfd array is mapped (was only range-check). */
    if (!access_ok_read(fds_ptr, nfds * sizeof(pollfd_t))) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    pollfd_t *pfds = (pollfd_t*)(uintptr_t)fds_ptr;
    extern u64 core_timer_ticks(void);
    u64 start_ticks = core_timer_ticks();
    for (;;) {
        int ready_count = 0;
        for (u64 i = 0; i < nfds; i++) {
            pfds[i].revents = 0;
            int fd = pfds[i].fd;
            if (fd < 0 || fd >= PROC_MAX_FDS) continue;
            sys_proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 0) continue;
            if (pfd->kind == 2) {
                sys_pipe_t *p = pipe_get(pfd->pipe_id);
                if (p && (pipe_data_avail(p) > 0 || p->writer_count == 0)) {
                    if (pfds[i].events & 0x001) pfds[i].revents |= 0x001;
                }
            } else if (pfd->kind == 1) {
                if (pfds[i].events & 0x001) pfds[i].revents |= 0x001;
            }
            if (pfds[i].revents) ready_count++;
        }
        if (ready_count > 0) return (u64)ready_count;
        if (timeout_ms != (u64)-1) {
            u64 elapsed = core_timer_ticks() - start_ticks;
            if (elapsed * 10 >= timeout_ms) return 0;
        }
        core_sched_yield();
    }
}

/* P0fix2 BUG-0033 (A2-3): write to a user VA through the TARGET address
 * space's page tables (software walk -> physical address), safe while CR3
 * still points at the kernel address space.  The old code dereferenced user
 * VAs directly under the kernel CR3, so the writes fell into the 0-4GiB
 * identity window (silently lost on a 512MiB box, live physical frames on
 * bigger machines). */
static void sys_execve_write_user(mem_vmm_as_t as, u64 va, const void *data, u64 len) {
    const u8 *src = (const u8 *)data;
    while (len > 0) {
        u64 phys = 0;
        if (!mem_vmm_is_mapped(as, va, &phys) || phys == 0) return;
        u64 off = va & 0xFFFULL;
        u64 chunk = PMM_PAGE_SIZE - off;
        if (chunk > len) chunk = len;
        memcpy((void *)(uintptr_t)(phys + off), src, (usize)chunk);
        src += chunk;
        va += chunk;
        len -= chunk;
    }
}

/* BUG-0249 FIX (A16-11): single name -> embedded-program lookup shared
 * by sys_execve and the L1 job API (job_create, l1/l1_wp8cd.c). One
 * table means the two paths can never disagree about what a program
 * name resolves to. Returns the ELF pointer and stores its size in
 * *size_out (when size_out != 0), or NULL for an unknown name. */
const u8 *core_userprog_lookup(const char *name, u64 *size_out) {
    extern const u8 userprog_hello[];
    extern const u8 userprog_fork_test[];
    extern const u8 userprog_exec_test[];
    extern const u8 userprog_pipe_test[];
    extern const u8 userprog_mmap_test[];
    extern const u8 userprog_signal_test[];
    extern const u8 userprog_select_test[];
    extern const u8 userprog_dyn_hello[];
    extern const u8 userprog_so_test[];
    extern const u8 userprog_dlsym_test[];
    extern const u8 userprog_pie_test[];
    extern const u8 userprog_reloc_test[];
    extern const u8 userprog_main_dyn[];
    extern const u8 userprog_mmap_multi[];
    extern const u8 userprog_ush[];
    extern const u8 userprog_fdref_test[];
    extern const u8 userprog_select_zero_test[];
    extern const u8 userprog_loop[];
    /* WP-10-AUDIT_P2-fix3 G6 regression programs (BUG-0274..0280 evidence
     * suites) - merged from fix3-laneA so sys_execve and the L1 job API
     * resolve the same names the `run` command does (BUG-0249 one-table
     * invariant). */
    extern const u8 userprog_g6_test[];
    extern const u8 userprog_g6_exec_a[];
    extern const u8 userprog_g6_exec_b[];
    extern const u8 userprog_g6_oom[];
    /* Sizes are defined next to the arrays in userprogs_data.h (only
     * included by core_usermode.c), so declare them here. */
    extern const u64 userprog_hello_size;
    extern const u64 userprog_fork_test_size;
    extern const u64 userprog_exec_test_size;
    extern const u64 userprog_pipe_test_size;
    extern const u64 userprog_mmap_test_size;
    extern const u64 userprog_signal_test_size;
    extern const u64 userprog_select_test_size;
    extern const u64 userprog_dyn_hello_size;
    extern const u64 userprog_so_test_size;
    extern const u64 userprog_dlsym_test_size;
    extern const u64 userprog_pie_test_size;
    extern const u64 userprog_reloc_test_size;
    extern const u64 userprog_main_dyn_size;
    extern const u64 userprog_mmap_multi_size;
    extern const u64 userprog_ush_size;
    extern const u64 userprog_fdref_test_size;
    extern const u64 userprog_select_zero_test_size;
    extern const u64 userprog_loop_size;
    extern const u64 userprog_g6_test_size;
    extern const u64 userprog_g6_exec_a_size;
    extern const u64 userprog_g6_exec_b_size;
    extern const u64 userprog_g6_oom_size;
    const u8 *elf = NULL;
    u64 size = 0;
    if (strcmp(name, "hello") == 0) { elf = userprog_hello; size = userprog_hello_size; }
    else if (strcmp(name, "fork_test") == 0) { elf = userprog_fork_test; size = userprog_fork_test_size; }
    else if (strcmp(name, "exec_test") == 0) { elf = userprog_exec_test; size = userprog_exec_test_size; }
    else if (strcmp(name, "pipe_test") == 0) { elf = userprog_pipe_test; size = userprog_pipe_test_size; }
    else if (strcmp(name, "mmap_test") == 0) { elf = userprog_mmap_test; size = userprog_mmap_test_size; }
    else if (strcmp(name, "signal_test") == 0) { elf = userprog_signal_test; size = userprog_signal_test_size; }
    else if (strcmp(name, "select_test") == 0) { elf = userprog_select_test; size = userprog_select_test_size; }
    else if (strcmp(name, "dyn_hello") == 0) { elf = userprog_dyn_hello; size = userprog_dyn_hello_size; }
    else if (strcmp(name, "so_test") == 0) { elf = userprog_so_test; size = userprog_so_test_size; }
    else if (strcmp(name, "dlsym_test") == 0) { elf = userprog_dlsym_test; size = userprog_dlsym_test_size; }
    else if (strcmp(name, "pie_test") == 0) { elf = userprog_pie_test; size = userprog_pie_test_size; }
    else if (strcmp(name, "reloc_test") == 0) { elf = userprog_reloc_test; size = userprog_reloc_test_size; }
    else if (strcmp(name, "main_dyn") == 0) { elf = userprog_main_dyn; size = userprog_main_dyn_size; }
    else if (strcmp(name, "mmap_multi") == 0) { elf = userprog_mmap_multi; size = userprog_mmap_multi_size; }
    else if (strcmp(name, "fdref_test") == 0) { elf = userprog_fdref_test; size = userprog_fdref_test_size; }
    else if (strcmp(name, "select_zero_test") == 0) { elf = userprog_select_zero_test; size = userprog_select_zero_test_size; }
    else if (strcmp(name, "loop") == 0) { elf = userprog_loop; size = userprog_loop_size; }
    else if (strcmp(name, "g6_test") == 0) { elf = userprog_g6_test; size = userprog_g6_test_size; }
    else if (strcmp(name, "g6_exec_a") == 0) { elf = userprog_g6_exec_a; size = userprog_g6_exec_a_size; }
    else if (strcmp(name, "g6_exec_b") == 0) { elf = userprog_g6_exec_b; size = userprog_g6_exec_b_size; }
    else if (strcmp(name, "g6_oom") == 0) { elf = userprog_g6_oom; size = userprog_g6_oom_size; }
    else if (strcmp(name, "ush") == 0 || strcmp(name, "usershell") == 0) {
        elf = userprog_ush; size = userprog_ush_size;
    }
    if (!elf) {
        /* BUG-0092 FIX (A16-5): fall back to the WP-08cd L1 tool table -
         * a tool registered via tool_register() is genuinely
         * executable by name. */
        extern int l1_wp8cd_tool_find(const char *name, const u8 **elf, u64 *size);
        u64 tool_size = 0;
        const u8 *tool_elf = NULL;
        if (l1_wp8cd_tool_find(name, &tool_elf, &tool_size) && tool_elf) {
            elf = tool_elf;
            size = tool_size;
        }
    }
    if (elf && size_out) *size_out = size;
    return elf;
}

static u64 sys_execve(u64 path, u64 argv, u64 envp, u64 a4) {
    /* P4 fix: actually USE argv (was ignored). The kernel still uses
     * embedded ELF byte arrays (no VFS file loading yet), but argv[0]
     * is now used to set up the user stack properly:
     *   argc = number of argv entries
     *   argv[] pointers (NULL-terminated)
     *   envp[] pointers (NULL-terminated)
     *   strings (the actual argv/envp content)
     * RSP points to argc on entry.
     *
     * If argv is NULL or argv[0] is NULL, we fall back to using `path`
     * as the program name (back-compat with old callers). */
    (void)a4;
    /* P3-9 FIX: path is a NUL-terminated string — copy it to a local
     * kernel buffer via copy_from_user so subsequent reads can't #PF
     * on user memory mid-exec. Max 256 bytes (longer is rejected). */
    char path_buf[256];
    if (!access_ok_str(path)) return (u64)-1;
    {
        const char *p = (const char*)(uintptr_t)path;
        u64 n = 0;
        while (n < 255 && p[n] != '\0') n++;
        if (copy_from_user(path_buf, path, n + 1) != 0) return (u64)-1;
        path_buf[n] = 0;
    }
    const char *name = path_buf;

    /* P4 fix: if argv is provided and argv[0] is non-NULL, use argv[0]
     * as the program name (POSIX semantics — `execv("/bin/ls", ["ls",
     * "-l"])` should resolve to "ls"). We strip /bin/ from argv[0]
     * the same way we do for path. */
    char argv0_buf[256];
    argv0_buf[0] = 0;
    if (argv != 0 && access_ok_read(argv, sizeof(u64))) {
        u64 argv0_ptr = *(u64*)(uintptr_t)argv;
        if (argv0_ptr != 0 && access_ok_str(argv0_ptr)) {
            const char *p = (const char*)(uintptr_t)argv0_ptr;
            u64 n = 0;
            while (n < 255 && p[n] != '\0') n++;
            if (copy_from_user(argv0_buf, argv0_ptr, n + 1) == 0) {
                argv0_buf[n] = 0;
            }
        }
    }
    /* If argv[0] was provided, use it as the program name. */
    if (argv0_buf[0] != 0) {
        /* Copy argv0_buf to path_buf (reuse the buffer). */
        int i = 0;
        while (i < 255 && argv0_buf[i] != 0) {
            path_buf[i] = argv0_buf[i];
            i++;
        }
        path_buf[i] = 0;
        name = path_buf;
    }

    /* Strip /bin/ prefix if present */
    if (name[0] == '/' && name[1] == 'b' && name[2] == 'i' &&
        name[3] == 'n' && name[4] == '/') name += 5;

    /* BUG-0249 FIX (A16-11): the name -> embedded-ELF chain moved into
     * core_userprog_lookup() so sys_execve and the L1 job API resolve
     * program names from ONE table (behavior unchanged here). */
    const u8 *elf = core_userprog_lookup(name, 0);
    if (!elf) return (u64)-1;

    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;

    /* P0fix2 BUG-0033 (A2-3) step 1: read argv/envp NOW, before the CR3
     * switch and before the old address space is destroyed.  Here CR3 is
     * still the caller's user address space and proc->as is the same one
     * access_ok_* validates, so both the checks and the dereferences see
     * the same, correct memory. */
    typedef struct { u8 data[32][256]; int len[32]; int count; } exec_args_buf_t;
    exec_args_buf_t *ab = (exec_args_buf_t *)kmalloc(sizeof(exec_args_buf_t));
    exec_args_buf_t *eb = (exec_args_buf_t *)kmalloc(sizeof(exec_args_buf_t));
    if (!ab || !eb) {
        if (ab) kfree(ab);
        if (eb) kfree(eb);
        return (u64)-1;
    }
    memset(ab, 0, sizeof(*ab));
    memset(eb, 0, sizeof(*eb));
    if (argv != 0) {
        for (int a = 0; a < 32; a++) {
            u64 entry_ptr = argv + (u64)a * sizeof(u64);
            if (!access_ok_read(entry_ptr, sizeof(u64))) break;
            u64 str_ptr = *(u64 *)(uintptr_t)entry_ptr;
            if (str_ptr == 0) break;
            if (!access_ok_str(str_ptr)) break;
            const char *sp = (const char *)(uintptr_t)str_ptr;
            int slen = 0;
            while (slen < 255 && sp[slen] != 0) slen++;
            memcpy(ab->data[a], sp, (usize)slen);
            ab->data[a][slen] = 0;
            ab->len[a] = slen;
            ab->count++;
        }
    }
    if (envp != 0) {
        for (int e = 0; e < 32; e++) {
            u64 entry_ptr = envp + (u64)e * sizeof(u64);
            if (!access_ok_read(entry_ptr, sizeof(u64))) break;
            u64 str_ptr = *(u64 *)(uintptr_t)entry_ptr;
            if (str_ptr == 0) break;
            if (!access_ok_str(str_ptr)) break;
            const char *sp = (const char *)(uintptr_t)str_ptr;
            int slen = 0;
            while (slen < 255 && sp[slen] != 0) slen++;
            memcpy(eb->data[e], sp, (usize)slen);
            eb->data[e][slen] = 0;
            eb->len[e] = slen;
            eb->count++;
        }
    }

    extern mem_vmm_as_t mem_vmm_kernel_as(void);
    mem_vmm_as_t old_as = proc->as;
    __asm__ volatile("mov %0, %%cr3" :: "r"(mem_vmm_kernel_as()) : "memory");
    if (old_as) mem_vmm_destroy_address_space(old_as);
    proc->as = create_user_address_space();
    if (proc->as == 0) {
        kfree(ab);
        kfree(eb);
        return (u64)-1;
    }

    typedef struct __attribute__((packed)) {
        u8 ident[16]; u16 type; u16 machine; u32 version; u64 entry; u64 phoff;
        u64 shoff; u32 flags; u16 ehsize; u16 phentsize; u16 phnum;
        u16 shentsize; u16 shnum; u16 shstrndx;
    } elf64_hdr_t;
    typedef struct __attribute__((packed)) {
        u32 type; u32 flags; u64 offset; u64 vaddr; u64 paddr;
        u64 filesz; u64 memsz; u64 align;
    } elf64_phdr_t;

    elf64_hdr_t *hdr = (elf64_hdr_t*)elf;
    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf + hdr->phoff);
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != 1) continue;
        map_user_pages(proc->as, phdr[i].vaddr, elf + phdr[i].offset, phdr[i].filesz);
        /* P6 fix: handle BSS (memsz > filesz). The old code only mapped
         * filesz bytes, leaving the BSS portion unmapped. Programs with
         * uninitialized globals (static variables in BSS) would #PF on
         * access. Now we zero-fill the extra pages, same as
         * user_process_create does. */
        if (phdr[i].memsz > phdr[i].filesz) {
            u64 bss_start = phdr[i].vaddr + phdr[i].filesz;
            u64 bss_end = bss_start + (phdr[i].memsz - phdr[i].filesz);
            u64 page = bss_start & ~0xFFFULL;
            while (page < bss_end) {
                u64 phys;
                if (!mem_vmm_is_mapped(proc->as, page, &phys)) {
                    phys = mem_pmm_alloc_frame();
                    if (phys) {
                        mem_vmm_map_page(proc->as, page, phys,
                                     VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
                        memset((void*)phys, 0, PMM_PAGE_SIZE);
                    }
                }
                page += PMM_PAGE_SIZE;
            }
        }
    }
    u64 stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (u64 vaddr = stack_base; vaddr < USER_STACK_TOP; vaddr += 0x1000) {
        u64 phys = mem_pmm_alloc_frame();
        if (phys) {
            mem_vmm_map_page(proc->as, vaddr, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            /* P0fix2 BUG-0033 (A2-3): zero the new stack pages (same as
             * user_process_create) so argc/argv reads never see old PMM
             * frame garbage. */
            memset((void *)(uintptr_t)phys, 0, PMM_PAGE_SIZE);
        }
    }
    proc->entry_point = hdr->entry;
    proc->brk = USER_BRK_BASE;
    proc->is_fork_child = 0;
    /* BUG-0277 FIX (A2-13a): POSIX exec resets all caught signals to
     * their default action and drops pending/queued signals. The old
     * code kept the PREVIOUS program's handler table, so a stale
     * user-space handler VA pointing into the destroyed image survived
     * exec; the next SIGUSR1-style delivery then iretq'd into whatever
     * bytes the NEW program happened to have at that address. */
    for (int i = 0; i < NSIG; i++) proc->sig_handlers[i] = NULL;
    proc->pending_signal = 0;
    proc->sig_in_progress = 0;

    /* P4 fix: build a proper user stack with argc + argv[] + envp[].
     * Layout (top to bottom, RSP grows down):
     *   [strings area]     <- strings for argv and envp, packed
     *   [envp ptrs]        <- NULL-terminated array of u64 pointers
     *   [argv ptrs]        <- NULL-terminated array of u64 pointers
     *   argc (u64)         <- RSP points here on entry
     *
     * P0fix2 BUG-0033 (A2-3) step 2: every write below goes through the NEW
     * address space's page tables via sys_execve_write_user(); the strings
     * come from the kernel buffers captured before the CR3 switch.  The old
     * code wrote the user stack VAs directly under the kernel CR3 identity
     * window, so the bytes landed outside the freshly mapped stack frames. */
    u64 rsp = USER_STACK_TOP;
    u64 argv_str_addrs[32];  /* max 32 argv entries */
    u64 envp_str_addrs[32];  /* max 32 envp entries */
    int argc = 0;
    /* Copy argv strings to top of stack (growing down), argv[0] highest. */
    for (int a = 0; a <= ab->count && a < 32; a++) {
        int slen = (a < ab->count) ? ab->len[a] : -1;
        if (a == ab->count) {
            /* Fallback when no argv[0] came through: use path (program name). */
            if (ab->count != 0) break;
            slen = 0;
            while (slen < 255 && path_buf[slen] != 0) slen++;
            rsp -= (u64)slen + 1;
            sys_execve_write_user(proc->as, rsp, path_buf, (u64)slen + 1);
            argv_str_addrs[argc++] = rsp;
            break;
        }
        rsp -= (u64)slen + 1;
        sys_execve_write_user(proc->as, rsp, ab->data[a], (u64)slen + 1);
        argv_str_addrs[argc++] = rsp;
    }
    /* Copy envp strings to stack. */
    int envc = 0;
    for (int e = 0; e <= eb->count && e < 32; e++) {
        if (e == eb->count) break;
        rsp -= (u64)eb->len[e] + 1;
        sys_execve_write_user(proc->as, rsp, eb->data[e], (u64)eb->len[e] + 1);
        envp_str_addrs[envc++] = rsp;
    }
    /* The buffers are fully consumed; free them before entering ring 3. */
    kfree(ab);
    kfree(eb);
    /* Align RSP to 16 bytes (ABI requirement). */
    rsp &= ~0xFFULL;
    /* Push envp[] pointer array (NULL-terminated). */
    {
        u64 zero = 0;
        rsp -= sizeof(u64);
        sys_execve_write_user(proc->as, rsp, &zero, sizeof(u64));
        for (int i = envc - 1; i >= 0; i--) {
            rsp -= sizeof(u64);
            sys_execve_write_user(proc->as, rsp, &envp_str_addrs[i], sizeof(u64));
        }
    }
    u64 envp_array_addr = rsp;
    /* Push argv[] pointer array (NULL-terminated). */
    {
        u64 zero = 0;
        rsp -= sizeof(u64);
        sys_execve_write_user(proc->as, rsp, &zero, sizeof(u64));
        for (int i = argc - 1; i >= 0; i--) {
            rsp -= sizeof(u64);
            sys_execve_write_user(proc->as, rsp, &argv_str_addrs[i], sizeof(u64));
        }
    }
    u64 argv_array_addr = rsp;
    /* Push argc. */
    {
        u64 argc_val = (u64)argc;
        rsp -= sizeof(u64);
        sys_execve_write_user(proc->as, rsp, &argc_val, sizeof(u64));
    }
    /* RSP now points at argc. argv_array_addr is at RSP+8, envp at the
     * appropriate offset. The user's _start can read argc from (RSP),
     * argv from (RSP+8), envp from (RSP+8 + (argc+1)*8). */
    (void)argv_array_addr; (void)envp_array_addr;
    proc->user_rsp = rsp;
    core_kthread_set_cr3(proc->tid, proc->as);
    __asm__ volatile("mov %0, %%cr3" :: "r"(proc->as) : "memory");
    enter_ring3(proc->entry_point, proc->user_rsp, proc->as);
    return 0;
}

/* WP-08b Batch 5: Solib name table for SYS_MAP_SOLIB.
 * Maps .so name → embedded ELF data.
 *
 * solib_libfoo and solib_libfoo_size are defined in usermode.c (which
 * #includes solib_data.h). They're not compile-time constants from
 * syscall.c's point of view (they're extern), so we can't put them in
 * a static-initializer. Instead we use a runtime-initialized table. */
struct solib_entry { const char *name; const u8 *data; u64 size; };
static struct solib_entry g_solib_table[1];
static int g_solib_table_inited = 0;
static void solib_table_init(void) {
    if (g_solib_table_inited) return;
    g_solib_table[0].name = "libfoo.so";
    g_solib_table[0].data = solib_libfoo;
    g_solib_table[0].size = solib_libfoo_size;
    g_solib_table_inited = 1;
}

/* WP-08b Batch 5: SYS_MAP_SOLIB — map a .so by name into user space.
 * Called by ld.so's dlopen() to dynamically load a shared library.
 * Args: rdi=name_ptr (user), rsi=name_len, rdx=flags (unused), r10=0
 * Returns: base address of the mapped .so, or 0 on failure.
 * Maps at proc->next_solib_addr (bump allocator from 0x38000000). */
static u64 sys_map_solib(u64 name_ptr, u64 name_len, u64 flags, u64 a4) {
    (void)flags; (void)a4;
    /* P3-9 FIX: validate name_ptr mapping (was only range-check). */
    if (!access_ok_read(name_ptr, name_len) || name_len == 0 || name_len > 64)
        return 0;
    solib_table_init();
    /* Copy name from user memory. */
    char name[64];
    const char *p = (const char *)(uintptr_t)name_ptr;
    u64 i;
    for (i = 0; i < name_len && i < 63; i++) {
        if (p[i] == '\0') break;
        name[i] = p[i];
    }
    name[i] = '\0';
    /* Look up in solib table. */
    const u8 *data = NULL; u64 size = 0;
    for (u64 j = 0; j < sizeof(g_solib_table)/sizeof(g_solib_table[0]); j++) {
        if (g_solib_table[j].name &&
            strcmp(name, g_solib_table[j].name) == 0) {
            data = g_solib_table[j].data;
            size = g_solib_table[j].size;
            break;
        }
    }
    if (!data) return 0;

    /* BUG-0280 FIX (A2-16): the inline ELF parse used to trust every
     * header field (e_phoff/e_phnum/p_offset/p_filesz/p_memsz/p_vaddr)
     * on the strength of "embedded blobs are trusted". That premise
     * dies the day the table is fed from anywhere else, and the
     * unchecked fields gave OOB kernel reads / wild mappings. Validate
     * the same fields user_process_create validates (P3-13/P3-14),
     * sized for this loader. */
    if (size < 64) return 0;
    /* Use raw casts (elf64_hdr_t is defined in usermode.c, not exported). */
    const u8 *hdr = data;
    if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F')
        return 0;
    if (hdr[4] != 2) return 0;  /* ELF64 */
    u16 e_type = *(u16*)(data + 16);
    if (e_type != 3) return 0;  /* ET_DYN */
    u64 e_phoff = *(u64*)(data + 32);
    u16 e_phentsize = *(u16*)(data + 54);
    u16 e_phnum = *(u16*)(data + 56);
    if (e_phentsize != 56) return 0;
    if (e_phnum == 0 || e_phnum > 64) return 0;
    {
        u64 phdr_table_end = e_phoff + (u64)e_phnum * 56;
        if (e_phoff >= size || phdr_table_end < e_phoff || phdr_table_end > size)
            return 0;
    }
    /* Find current process. */
    user_proc_t *proc = user_process_current();
    if (!proc) return 0;

    /* The dlopen bump window is [USER_SOLIB_BASE, USER_MMAP_BASE): the
     * documented per-process .so region below the mmap base. A bump that
     * walked past USER_MMAP_BASE would overwrite the process's (or a
     * future) mmap mappings, and a zero cursor would map at VA 0 — both
     * fail closed here. */
    u64 base = proc->next_solib_addr;
    if (base == 0) base = USER_SOLIB_BASE;
    if (base < USER_SOLIB_BASE || base >= USER_MMAP_BASE) return 0;

    /* Pass 1: validate every PT_LOAD and size the whole image. */
    #define SOLIB_MAX_SEGS 16
    u64 max_end = 0;
    int nseg = 0;
    for (int k = 0; k < e_phnum; k++) {
        const u8 *ph = data + e_phoff + (u64)k * 56;
        u32 p_type = *(u32*)(ph);
        if (p_type != 1) continue;  /* PT_LOAD */
        u64 p_offset = *(u64*)(ph + 8);
        u64 p_vaddr  = *(u64*)(ph + 16);
        u64 p_filesz = *(u64*)(ph + 32);
        u64 p_memsz  = *(u64*)(ph + 40);
        if (p_memsz < p_filesz) return 0;              /* negative BSS */
        u64 file_end = p_offset + p_filesz;
        if (file_end < p_offset || file_end > size) return 0;
        u64 mem_end = p_vaddr + p_memsz;
        if (mem_end < p_vaddr) return 0;               /* overflow */
        if (p_vaddr >= USER_MMAP_BASE) return 0;       /* not a solib VA */
        u64 seg_end = (mem_end + 0xFFFULL) & ~0xFFFULL;
        if (seg_end > max_end) max_end = seg_end;
        if (++nseg > SOLIB_MAX_SEGS) return 0;
    }
    if (nseg == 0) return 0;
    /* Window cap: fail closed BEFORE touching any page table if this
     * .so would not fit in the remaining solib window. On failure the
     * bump cursor is not advanced, so a later dlclose-less retry of a
     * smaller image (or the same image after the region is drained)
     * still lands correctly. */
    if (base + max_end > USER_MMAP_BASE) return 0;

    /* Pass 2: map. Every page this call installs is recorded so a
     * mid-way failure (map_user_pages short, frame OOM) unmaps and
     * frees exactly what it installed - the old code returned 0 with
     * the already-mapped segments left behind (PTE + frame leak) and
     * an unadvanced cursor, so the NEXT dlopen re-mapped the same range
     * over the surviving PTEs and leaked the old frames again. */
    typedef struct { u64 start; u64 pages; } solib_seg_roll_t;
    solib_seg_roll_t roll[SOLIB_MAX_SEGS];
    int nroll = 0;
    for (int k = 0; k < e_phnum; k++) {
        const u8 *ph = data + e_phoff + (u64)k * 56;
        u32 p_type = *(u32*)(ph);
        if (p_type != 1) continue;
        u64 p_offset = *(u64*)(ph + 8);
        u64 p_vaddr  = *(u64*)(ph + 16);
        u64 p_filesz = *(u64*)(ph + 32);
        u64 p_memsz  = *(u64*)(ph + 40);
        u64 load_vaddr = base + p_vaddr;
        u64 seg_start = load_vaddr & ~0xFFFULL;
        u64 seg_pages = ((load_vaddr + p_memsz + 0xFFFULL) & ~0xFFFULL) - seg_start;
        roll[nroll].start = seg_start;
        roll[nroll].pages = seg_pages;
        nroll++;
        if (map_user_pages(proc->as, load_vaddr,
                          data + p_offset, p_filesz) != 0)
            goto solib_fail;
        /* bss zero pages. */
        if (p_memsz > p_filesz) {
            u64 bs = load_vaddr + p_filesz;
            u64 be = load_vaddr + p_memsz;
            u64 page = bs & ~0xFFFULL;
            while (page < be) {
                u64 phys;
                if (!mem_vmm_is_mapped(proc->as, page, &phys)) {
                    phys = mem_pmm_alloc_frame();
                    if (phys == 0) goto solib_fail;   /* OOM: roll back */
                    if (mem_vmm_map_page(proc->as, page, phys,
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER) != 0) {
                        mem_pmm_free_frame(phys);
                        goto solib_fail;
                    }
                    memset((void*)phys, 0, PMM_PAGE_SIZE);
                }
                page += PMM_PAGE_SIZE;
            }
        }
        continue;
    solib_fail:
        for (int s = 0; s < nroll; s++) {
            for (u64 pg = 0; pg < roll[s].pages; pg++) {
                u64 old_pte = mem_vmm_unmap_page(proc->as,
                                                 roll[s].start + pg * 0x1000);
                if (old_pte & VMM_FLAG_PRESENT) {
                    u64 fr = old_pte & 0x000FFFFFFFFFF000ULL;
                    if (fr != 0) mem_pmm_free_frame(fr);
                }
            }
        }
        return 0;   /* next_solib_addr unchanged: no leak, no overlap */
    }
    /* Advance bump allocator (guaranteed <= USER_MMAP_BASE by pass 1). */
    proc->next_solib_addr = base + max_end;
    return base;
    #undef SOLIB_MAX_SEGS
}

/* WP-08cd forward declarations */
static u64 sys_open(u64 path, u64 flags, u64 a3, u64 a4);
static u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4);
static u64 sys_stat(u64 path, u64 stat_buf, u64 a3, u64 a4);
static u64 sys_readdir(u64 path, u64 index, u64 dirent_buf, u64 a4);
static u64 sys_mkdir(u64 path, u64 a2, u64 a3, u64 a4);
static u64 sys_rmdir(u64 path, u64 a2, u64 a3, u64 a4);
static u64 sys_unlink(u64 path, u64 a2, u64 a3, u64 a4);
static u64 sys_write2(u64 fd, u64 buf, u64 len, u64 a4);
static u64 sys_readline(u64 buf, u64 maxlen, u64 a3, u64 a4);

static u64 sys_getch(u64 a1, u64 a2, u64 a3, u64 a4);
static u64 sys_uptime(u64 a1, u64 a2, u64 a3, u64 a4);
static u64 sys_meminfo(u64 buf, u64 a2, u64 a3, u64 a4);
void core_syscall_wp08a_init(void) {
    core_syscall_register(SYS_FORK, sys_fork);
    core_syscall_register(SYS_EXECVE, sys_execve);
    core_syscall_register(SYS_WAIT4, sys_wait4);
    core_syscall_register(SYS_KILL, sys_kill);
    core_syscall_register(SYS_GETPID, sys_getpid);
    core_syscall_register(SYS_GETPPID, sys_getppid);
    core_syscall_register(SYS_EXIT2, sys_exit2);
    core_syscall_register(SYS_PIPE, sys_pipe);
    core_syscall_register(SYS_DUP, sys_dup);
    core_syscall_register(SYS_DUP2, sys_dup2);
    core_syscall_register(SYS_MMAP, sys_mem_mmap);
    core_syscall_register(SYS_MUNMAP, sys_mem_munmap);
    core_syscall_register(SYS_MPROTECT, sys_mem_mprotect);
    core_syscall_register(SYS_BRK, sys_mem_brk);
    core_syscall_register(SYS_SIGNAL, sys_signal);
    core_syscall_register(SYS_SIGACTION, sys_sigaction);
    core_syscall_register(SYS_SIGRETURN, sys_sigreturn);
    core_syscall_register(SYS_CHDIR, sys_chdir);
    core_syscall_register(SYS_GETCWD, sys_getcwd);
    core_syscall_register(SYS_IOCTL, sys_ioctl);
    core_syscall_register(SYS_READ, sys_read);
    core_syscall_register(SYS_SELECT, sys_select);
    core_syscall_register(SYS_POLL, sys_poll);
    core_syscall_register(SYS_WRITE, sys_write);
    core_syscall_register(SYS_MAP_SOLIB, sys_map_solib);  /* WP-08b Batch 5 */
    /* WP-08cd: File operations + shell support */
    core_syscall_register(SYS_OPEN, sys_open);
    core_syscall_register(SYS_CLOSE, sys_close);
    core_syscall_register(SYS_STAT, sys_stat);
    core_syscall_register(SYS_READDIR, sys_readdir);
    core_syscall_register(SYS_MKDIR, sys_mkdir);
    core_syscall_register(SYS_RMDIR, sys_rmdir);
    core_syscall_register(SYS_UNLINK, sys_unlink);
    core_syscall_register(SYS_WRITE2, sys_write2);
    core_syscall_register(SYS_READLINE, sys_readline);
    core_syscall_register(SYS_GETCH, sys_getch);
    core_syscall_register(SYS_UPTIME, sys_uptime);  /* P2-04 */
    core_syscall_register(SYS_MEMINFO, sys_meminfo);  /* WP-09-FIX BUG-014 */
    memset(g_pipes, 0, sizeof(g_pipes));
}

/* ============================================================
 * WP-08cd: File operations + user-space shell support
 * ============================================================ */

/* SYS_OPEN(3): open a file path, return fd >= 0 or -1.
 * Maps to fs_vfs_open. flags: 0=read, 1=write, 2=read+write. */
static u64 sys_open(u64 path, u64 flags, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    /* P5 fix: resolve relative paths against cwd before passing to VFS.
     * Old code passed "test.txt" directly to fs_vfs_open which rejected it
     * because fs_vfs_normalize requires absolute paths. */
    char resolved[256];
    if (resolve_user_path(path, resolved, sizeof(resolved)) < 0) return (u64)-1;
    int fs_vfs_fd = fs_vfs_open(resolved, (int)flags);
    if (fs_vfs_fd < 0) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    for (int i = 3; i < PROC_MAX_FDS; i++) {
        if (proc->fds[i].kind == 0) {
            proc->fds[i].kind = 1;
            proc->fds[i].fs_vfs_fd = fs_vfs_fd;
            return (u64)i;
        }
    }
    fs_vfs_close(fs_vfs_fd);
    return (u64)-1;  /* no free fd */
}

/* SYS_CLOSE(4): close a file descriptor. */
/* SYS_CLOSE(4): close a file descriptor.
 * BUG-001 FIX (P0): For pipe fds (kind 2/3), decrement reader_count /
 * writer_count and wake waiters. Without this, the count never reaches 0
 * so sys_read's EOF branch (`if (writer_count == 0) return 0`) is never
 * taken, causing permanent deadlock after any `cmd1 | cmd2` in ush. */
static u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    sys_proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) return (u64)-1;
    int saved_kind = pfd->kind;
    int saved_pipe_id = pfd->pipe_id;
    int saved_vfs_fd = pfd->fs_vfs_fd;
    /* Mark free first so concurrent ops don't see stale state */
    pfd->kind = 0;
    pfd->pipe_id = 0;
    pfd->fs_vfs_fd = 0;
    if (saved_kind == 1) {
        fs_vfs_close(saved_vfs_fd);
    } else if (saved_kind == 2 || saved_kind == 3) {
        /* Decrement pipe reference count and wake any waiters */
        sys_pipe_t *pp = pipe_get(saved_pipe_id);
        if (pp) {
            if (saved_kind == 2) pp->reader_count--;
            else                  pp->writer_count--;
            /* P3-15: wake ALL waiters on close. */
            pipe_wake_all(pp->reader_waiters);
            pipe_wake_all(pp->writer_waiters);
        }
    }
    return 0;
}

/* SYS_STAT(5): stat a file path. stat_buf points to a user buffer
 * of at least sizeof(fs_vfs_stat_t) bytes. Returns 0 or -1. */
static u64 sys_stat(u64 path, u64 stat_buf, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    /* P5 fix: resolve relative paths. */
    char resolved[256];
    if (resolve_user_path(path, resolved, sizeof(resolved)) < 0) return (u64)-1;
    if (!access_ok_read(stat_buf, sizeof(fs_vfs_stat_t))) return (u64)-1;
    fs_vfs_stat_t st;
    if (fs_vfs_stat(resolved, &st) < 0) return (u64)-1;
    if (copy_to_user(stat_buf, &st, sizeof(st)) != 0) return (u64)-1;
    return 0;
}

/* SYS_READDIR(6): read directory entry by index.
 * (path, index, dirent_buf) → 0 on success, -1 on end/error. */
static u64 sys_readdir(u64 path, u64 index, u64 dirent_buf, u64 a4) {
    (void)a4;
    /* P5 fix: resolve relative paths. */
    char resolved[256];
    if (resolve_user_path(path, resolved, sizeof(resolved)) < 0) return (u64)-1;
    if (!access_ok_read(dirent_buf, sizeof(fs_vfs_dirent_t))) return (u64)-1;
    fs_vfs_dirent_t e;
    if (fs_vfs_readdir(resolved, (int)index, &e) < 0)
        return (u64)-1;
    if (copy_to_user(dirent_buf, &e, sizeof(e)) != 0) return (u64)-1;
    return 0;
}

/* SYS_MKDIR(7): create a directory. */
static u64 sys_mkdir(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    /* P5 fix: resolve relative paths. */
    char resolved[256];
    if (resolve_user_path(path, resolved, sizeof(resolved)) < 0) return (u64)-1;
    return (u64)fs_vfs_mkdir(resolved);
}

static u64 sys_rmdir(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    /* P5 fix: resolve relative paths. */
    char resolved[256];
    if (resolve_user_path(path, resolved, sizeof(resolved)) < 0) return (u64)-1;
    return (u64)fs_vfs_rmdir(resolved);
}

static u64 sys_unlink(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    /* P5 fix: resolve relative paths. */
    char resolved[256];
    if (resolve_user_path(path, resolved, sizeof(resolved)) < 0) return (u64)-1;
    return (u64)fs_vfs_unlink(resolved);
}

/* SYS_WRITE2(72): fd-aware write.
 * (fd, buf, len) → bytes written or -1.
 * fd=1 → console (stdout), fd=2 → console (stderr),
 * fd=pipe_write → pipe, fd=vfs → fs_vfs_write. */
static u64 sys_write2(u64 fd, u64 buf, u64 len, u64 a4) {
    (void)a4;
    /* P3-9 FIX: verify buf mapping (was only range-check). */
    if (!access_ok_read(buf, len)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    sys_proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) {
        /* fd not open: check if it's stdout/stderr (1 or 2) */
        if (fd == 1 || fd == 2) {
            /* BUG-0276 FIX (A2-12): same cli-atomic emission window as
             * the sys_write console fallback (WP-09-FIX BUG-023). The
             * old write2 path emitted character-by-character with
             * interrupts on, so concurrent user processes interleaved
             * on the serial console; the two write syscalls now behave
             * identically. */
            const char *p = (const char*)(uintptr_t)buf;
            u64 done = 0;
            while (done < len) {
                u64 chunk = len - done;
                if (chunk > 512) chunk = 512;
                u64 eflags;
                __asm__ volatile("pushfq; popq %0; cli" : "=r"(eflags));
                for (u64 i = 0; i < chunk; i++) screen_console_putc(p[done + i]);
                __asm__ volatile("pushq %0; popfq" : : "r"(eflags));
                done += chunk;
            }
            return len;
        }
        return (u64)-1;
    }
    if (pfd->kind == 1) {
        /* VFS fd: write to file */
        return (u64)fs_vfs_write(pfd->fs_vfs_fd, (const void*)(uintptr_t)buf, (int)len);
    }
    if (pfd->kind == 3) {
        /* pipe-write fd */
        sys_pipe_t *p = pipe_get(pfd->pipe_id);
        if (!p) return (u64)-1;
        const u8 *src = (const u8*)(uintptr_t)buf;
        u64 written = 0;
        while (written < len) {
            u32 space = pipe_space_avail(p);
            if (space == 0) {
                /* BUG-0276 FIX (A2-12): mirror sys_write's P6 rule for a
                 * FULL pipe with NO reader: fail instead of blocking
                 * forever. The old write2 branch unconditionally joined
                 * the writer wait queue, so a full pipe whose reader had
                 * gone parked the writer permanently (pipe_wait_all only
                 * fires on close of a remaining end). Same semantics as
                 * sys_write: with buffer space left, the data is still
                 * accepted even with 0 readers (a later forked reader
                 * may pick it up); with a full buffer and no reader the
                 * data can never be consumed - return what was written
                 * (or -1 if nothing). */
                if (p->reader_count == 0) {
                    return written > 0 ? written : (u64)-1;
                }
                /* P3-15: wait queue (FIFO), not single slot. */
                int my_tid = core_kthread_current_tid();
                pipe_wait_add(p->writer_waiters, my_tid);
                core_kthread_block();
                pipe_wait_remove(p->writer_waiters, my_tid);
                space = pipe_space_avail(p);
                if (space == 0) break;
            }
            u32 to_copy = (u32)((len - written < space) ? (len - written) : space);
            u32 wpos = p->write_pos % PIPE_BUF_SIZE;
            if (wpos + to_copy > PIPE_BUF_SIZE) to_copy = PIPE_BUF_SIZE - wpos;
            memcpy(p->buf + wpos, src + written, to_copy);
            p->write_pos += to_copy;
            written += to_copy;
            /* P3-15: wake ONE reader (FIFO). */
            pipe_wake_one(p->reader_waiters);
        }
        return written;
    }
    return (u64)-1;
}

/* SYS_READLINE(73): read a line from keyboard.
 * (buf, maxlen) → bytes read (not including \n) or 0.
 * WP-08cd: Reads raw characters from keyboard queue, echoes them,
 * handles backspace. Does NOT use the kernel's global readline
 * state (g_line/g_line_ready) to avoid interference between
 * the kernel shell and ush. */
static u64 sys_readline(u64 buf, u64 maxlen, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    /* P3-9 FIX: verify buf mapping (was only range-check). */
    if (maxlen == 0) return 0;
    if (maxlen > 255) maxlen = 255;
    if (!access_ok_read(buf, maxlen)) return (u64)-1;
    char line[256];
    int len = 0;
    for (;;) {
        /* Enable interrupts and halt until a key arrives */
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        int k = driver_input_keyboard_getch();
        if (k < 0) continue;
        if (k == 0x0A || k == 0x0D) {
            /* Enter: finish line */
            screen_console_putc('\n');
            break;
        }
        if (k == 0x08 || k == 0x7F) {
            /* Backspace */
            if (len > 0) {
                len--;
                screen_console_putc('\b');
                screen_console_putc(' ');
                screen_console_putc('\b');
            }
            continue;
        }
        if (k >= 0x20 && k < 0x7F && len < (int)maxlen - 1) {
            line[len++] = (char)k;
            screen_console_putc((char)k);
        }
    }
    line[len] = 0;
    memcpy((void*)(uintptr_t)buf, line, (u64)len);
    ((char*)(uintptr_t)buf)[len] = 0;  /* null-terminate user buffer */
    return (u64)len;
}

/* SYS_GETCH(74): read one raw character from keyboard (no echo, no line edit).
 * Returns the ASCII key, or -1 if no key available. Non-blocking. */
static u64 sys_getch(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    int k = driver_input_keyboard_getch();
    if (k < 0) return (u64)-1;
    return (u64)k;
}

/* P2-04 FIX: SYS_UPTIME(75): return milliseconds since boot from the kernel
 * PIT-driven tick counter. Replaces the hardcoded date string in ush. */
static u64 sys_uptime(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    return core_timer_now_ms();
}

/* WP-09-FIX BUG-014: SYS_MEMINFO(76): fill a user buffer with REAL PMM
 * numbers (total/used/free bytes) so ush `free` no longer prints
 * hardcoded fake data (it claimed 128 MiB total on a 512 MiB VM). */
static u64 sys_meminfo(u64 buf, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    mem_pmm_stats_t s;
    mem_pmm_get_stats(&s);
    u64 out[3];
    out[0] = s.total_bytes;
    out[1] = s.used_bytes;
    out[2] = s.free_bytes;
    if (copy_to_user(buf, out, sizeof(out)) < 0) return (u64)-1;
    return 0;
}

/* ============================================================
 * WP-10-wp08fix1: links / permissions / net bridge / process table
 * Syscalls 96-102 backing the new ush toolset. All of them route to
 * the REAL kernel implementations (fs_vfs_*, the WP-06 network shell
 * commands, the scheduler task table) - no stubs.
 * ============================================================ */

/* SYS_SYMLINK(96): create a symbolic link. (target, linkpath) -> 0/-1. */
static u64 sys_symlink(u64 target, u64 linkpath, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    char t[256], l[256];
    if (resolve_user_path(target, t, sizeof(t)) < 0) return (u64)-1;
    if (resolve_user_path(linkpath, l, sizeof(l)) < 0) return (u64)-1;
    /* WP-10-wp08fix1: pass the real fs_vfs error code through (the ush
     * tools print it for diagnostics). */
    return (u64)(long)fs_vfs_symlink(t, l);
}

/* SYS_READLINK(97): read a symlink target. (path, buf, cap) -> len/-1. */
static u64 sys_readlink(u64 path, u64 buf, u64 cap, u64 a4) {
    (void)a4;
    char p[256];
    if (!valid_user_ptr(buf) || cap == 0 || cap > 1024) return (u64)-1;
    if (resolve_user_path(path, p, sizeof(p)) < 0) return (u64)-1;
    char target[256];
    int n = fs_vfs_readlink(p, target, sizeof(target));
    if (n < 0) return (u64)-1;
    if (n > (int)cap - 1) n = (int)cap - 1;
    if (copy_to_user(buf, target, (u64)n) < 0) return (u64)-1;
    /* NUL-terminate for the user (the byte after the copied target). */
    {
        char z = 0;
        copy_to_user(buf + n, &z, 1);
    }
    return (u64)n;
}

/* SYS_LINK(98): create a hard link. (oldpath, newpath) -> 0/-1. */
static u64 sys_link(u64 oldpath, u64 newpath, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    char o[256], nn[256];
    if (resolve_user_path(oldpath, o, sizeof(o)) < 0) return (u64)-1;
    if (resolve_user_path(newpath, nn, sizeof(nn)) < 0) return (u64)-1;
    return (u64)(long)fs_vfs_link(o, nn);
}

/* SYS_CHMOD(99): change permission bits. (path, mode) -> 0/-1. */
static u64 sys_chmod(u64 path, u64 mode, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    char p[256];
    if (resolve_user_path(path, p, sizeof(p)) < 0) return (u64)-1;
    return (u64)(long)fs_vfs_chmod(p, (u32)(mode & 07777));
}

/* SYS_CHOWN(100): change owner. (path, uid, gid) -> 0/-1. */
static u64 sys_chown(u64 path, u64 uid, u64 gid, u64 a4) {
    (void)a4;
    char p[256];
    if (resolve_user_path(path, p, sizeof(p)) < 0) return (u64)-1;
    return (u64)(long)fs_vfs_chown(p, (u32)uid, (u32)gid);
}

/* Copy a NUL-terminated string from user memory. Returns 0 on success,
 * -1 if unmapped/too long. */
static int copy_user_str(u64 src, char *dst, int cap) {
    if (!access_ok_str(src)) return -1;
    const char *p = (const char*)(uintptr_t)src;
    u64 n = 0;
    while (n < (u64)cap - 1 && p[n] != '\0') n++;
    if (p[n] != '\0') return -1;             /* ran past cap */
    if (copy_from_user(dst, src, n + 1) != 0) return -1;
    dst[n] = 0;
    return 0;
}

/* SYS_NETCMD(101): run a WP-06 network shell command in the kernel and
 * capture its REAL output for the user-space tools (ifconfig/ping/
 * netstat/wget). This reuses shell_execute_captured - the exact same
 * code path the oc> commands use - instead of duplicating the protocol
 * logic. (op, arg, out, cap) -> 0/-1. */
static u64 sys_netcmd(u64 op, u64 arg, u64 out, u64 cap) {
    char cmdline[300];
    if (!valid_user_ptr(out) || cap == 0 || cap > 8192) return (u64)-1;
    switch (op) {
        case NETCMD_IFCONFIG:
            strcpy(cmdline, "ifconfig");
            break;
        case NETCMD_NETSTAT:
            strcpy(cmdline, "netstat");
            break;
        case NETCMD_PING: {
            char host[200];
            if (copy_user_str(arg, host, sizeof(host)) < 0) return (u64)-1;
            strcpy(cmdline, "ping ");
            strncpy(cmdline + 5, host, sizeof(cmdline) - 6);
            cmdline[sizeof(cmdline) - 1] = 0;
            break;
        }
        case NETCMD_WGET: {
            char args[250];
            if (copy_user_str(arg, args, sizeof(args)) < 0) return (u64)-1;
            strcpy(cmdline, "wget ");
            strncpy(cmdline + 5, args, sizeof(cmdline) - 6);
            cmdline[sizeof(cmdline) - 1] = 0;
            break;
        }
        default:
            return (u64)-1;
    }
    {
        /* WP-10-wp08fix1: kmalloc'd per call (was an 8 KiB static - the
         * kernel .bss must stay below the 0x400000 identity-window
         * boundary, see editor.c). */
        char *kout = (char *)kmalloc(8192);
        if (!kout) return (u64)-1;
        shell_execute_captured(cmdline, kout, 8192);
        int n = (int)strlen(kout);
        if (n > (int)cap - 1) n = (int)cap - 1;
        int rc = copy_to_user(out, kout, (u64)n);
        if (rc == 0) {
            char z = 0;
            copy_to_user(out + n, &z, 1);
        }
        kfree(kout);
        if (rc < 0) return (u64)-1;
    }
    return 0;
}

/* SYS_PS(102): copy the live task table to user space.
 * (buf, cap) -> entry count / -1. The kernel-side entry layout is
 * core_sched_task_info_t (tid/state/priority/name[32]/cpu ticks); the
 * user-side mirror lives in userprogs/ush.c. */
static u64 sys_ps(u64 buf, u64 cap, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    if (!valid_user_ptr(buf)) return (u64)-1;
    if (cap < sizeof(core_sched_task_info_t) ||
        cap > sizeof(core_sched_task_info_t) * 64) return (u64)-1;
    int max = (int)(cap / sizeof(core_sched_task_info_t));
    {
        /* P0fix2 BUG-0034 (A2-4): ktab was a fixed 3584-byte stack array on
         * a small kernel thread stack (entry frame plus a nested timer IRQ
         * on top left almost no headroom on a 1-page stack).  Size it for
         * the caller's cap and take it from the heap instead. */
        core_sched_task_info_t *ktab =
            (core_sched_task_info_t *)kmalloc(sizeof(core_sched_task_info_t) * (u64)max);
        if (!ktab) return (u64)-1;
        int n = core_sched_task_info_get(ktab, max);
        if (n < 0) { kfree(ktab); return (u64)-1; }
        if (copy_to_user(buf, ktab, (u64)n * sizeof(core_sched_task_info_t)) < 0) {
            kfree(ktab);
            return (u64)-1;
        }
        kfree(ktab);
        return (u64)n;
    }
}

/* Register the WP-10-wp08fix1 syscalls. Called from usermode_init(). */
void core_syscall_wp08fix1_init(void) {
    core_syscall_register(SYS_SYMLINK, sys_symlink);
    core_syscall_register(SYS_READLINK, sys_readlink);
    core_syscall_register(SYS_LINK, sys_link);
    core_syscall_register(SYS_CHMOD, sys_chmod);
    core_syscall_register(SYS_CHOWN, sys_chown);
    core_syscall_register(SYS_NETCMD, sys_netcmd);
    core_syscall_register(SYS_PS, sys_ps);
}
