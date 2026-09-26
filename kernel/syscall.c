/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-08a
 * File: kernel/syscall.c
 * Purpose: Complete syscall set — process management, IPC, signals, memory,
 *          file/directory, and I/O multiplexing.
 */
#include "syscall.h"
#include "usermode.h"
#include "sched.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "vfs.h"
#include "console.h"
#include "console_in.h"
#include "string.h"
#include "keyboard.h"
#include "shell.h"
#include "idt.h"

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

/* ---- Pipe implementation ---- */
#define PIPE_BUF_SIZE 4096
#define MAX_PIPES 32

typedef struct {
    u8 buf[PIPE_BUF_SIZE];
    u32 read_pos;
    u32 write_pos;
    int in_use;
    int reader_count;
    int writer_count;
    int reader_waiting;
    int writer_waiting;
} kernel_pipe_t;

static kernel_pipe_t g_pipes[MAX_PIPES];

static kernel_pipe_t *pipe_get(int pipe_id) {
    if (pipe_id < 1 || pipe_id > MAX_PIPES) return NULL;
    if (!g_pipes[pipe_id - 1].in_use) return NULL;
    return &g_pipes[pipe_id - 1];
}

static int pipe_alloc(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!g_pipes[i].in_use) {
            oc_memset(&g_pipes[i], 0, sizeof(g_pipes[i]));
            g_pipes[i].in_use = 1;
            g_pipes[i].reader_waiting = -1;
            g_pipes[i].writer_waiting = -1;
            return i + 1;
        }
    }
    return 0;
}

static u32 pipe_data_avail(kernel_pipe_t *p) { return p->write_pos - p->read_pos; }
static u32 pipe_space_avail(kernel_pipe_t *p) { return PIPE_BUF_SIZE - pipe_data_avail(p); }

static int proc_fd_alloc(user_proc_t *proc) {
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
        p->alive = 0;
        p->exit_code = (int)code;
        /* WP-08cd: Clear usershell running flag ONLY when the ush
         * process itself exits (not when pipe children exit).
         * The ush process has no parent user process (parent_tid=0
         * because it was started from the kernel shell). Pipe children
         * have parent_tid = ush's tid (> 0), so they don't clear it. */
        extern int g_usershell_running;
        if (p->parent_tid == 0) {
            g_usershell_running = 0;
        }
        /* WP-08cd FIX: Close all pipe fds so pipe reader/writer counts
         * are decremented. Without this, the reader blocks forever
         * waiting for a writer that already exited. */
        for (int i = 0; i < PROC_MAX_FDS; i++) {
            if (p->fds[i].kind == 2 || p->fds[i].kind == 3) {
                kernel_pipe_t *pp = pipe_get(p->fds[i].pipe_id);
                if (pp) {
                    if (p->fds[i].kind == 2) pp->reader_count--;
                    else pp->writer_count--;
                    /* Wake anyone waiting on this pipe */
                    if (pp->reader_waiting >= 0) kthread_wake(pp->reader_waiting);
                    if (pp->writer_waiting >= 0) kthread_wake(pp->writer_waiting);
                }
                p->fds[i].kind = 0;
            }
        }
        if (p->parent_tid > 0) kthread_wake(p->parent_tid);
        /* BUG-008 FIX: Destroy user address space before exiting.
         * Switch to kernel CR3 first, then destroy. */
        if (p->as) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_as()) : "memory");
            vmm_destroy_address_space(p->as);
            p->as = 0;
        }
    }
    task_t *t = kthread_current();
    if (t) t->state = TASK_EXITED;
    for (;;) kthread_block();
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
    oc_irq_frame_t *f = (oc_irq_frame_t*)frame;

    int child_idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!g_procs[i].alive) { child_idx = i; break; }
    }
    if (child_idx < 0) return (u64)-1;

    user_proc_t *child = &g_procs[child_idx];
    oc_memset(child, 0, sizeof(*child));
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
    oc_strncpy(child->name, parent->name, 31);

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
            kernel_pipe_t *p = pipe_get(child->fds[i].pipe_id);
            if (p) p->reader_count++;
        } else if (child->fds[i].kind == 3) {
            kernel_pipe_t *p = pipe_get(child->fds[i].pipe_id);
            if (p) p->writer_count++;
        }
    }

    child->tid = kthread_create(user_task_launcher, child, child->name, TASK_PRIO_DEFAULT);
    if (child->tid < 0) { child->alive = 0; return (u64)-1; }
    kthread_set_cr3(child->tid, child->as);

    return (u64)child->pid;
}

/* ============================================================
 * SYS_WAIT4 (12)
 * ============================================================ */
static u64 sys_wait4(u64 pid_arg, u64 status_ptr, u64 options, u64 a4) {
    (void)options;(void)a4;
    tid_t parent_tid = kthread_current_tid();
    for (;;) {
        int found_alive = 0;
        for (int i = 0; i < MAX_USER_PROCS; i++) {
            if (g_procs[i].parent_tid != parent_tid) continue;
            if (g_procs[i].pid == 0) continue;
            if ((i64)pid_arg > 0 && g_procs[i].pid != (pid_t)pid_arg) continue;
            if (!g_procs[i].alive && !g_procs[i].waited) {
                g_procs[i].waited = 1;
                if (status_ptr && valid_user_ptr(status_ptr))
                    *(int*)(uintptr_t)status_ptr = g_procs[i].exit_code;
                kthread_destroy(g_procs[i].tid);
                return (u64)g_procs[i].pid;
            }
            if (g_procs[i].alive) found_alive = 1;
        }
        if (!found_alive) return (u64)-1;
        kthread_block();
    }
}

static u64 sys_kill(u64 pid_arg, u64 sig, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    if (sig >= NSIG) return (u64)-1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].pid == (pid_t)pid_arg) {
            if (sig == SIGKILL) {
                g_procs[i].alive = 0;
                g_procs[i].exit_code = 128 + (int)sig;
                if (g_procs[i].parent_tid > 0) kthread_wake(g_procs[i].parent_tid);
                task_t *t = kthread_get_task(g_procs[i].tid);
                if (t) t->state = TASK_EXITED;
                return 0;
            }
            g_procs[i].pending_signal = (int)sig;
            if (g_procs[i].sig_handlers[sig] == NULL) {
                g_procs[i].alive = 0;
                g_procs[i].exit_code = 128 + (int)sig;
                if (g_procs[i].parent_tid > 0) kthread_wake(g_procs[i].parent_tid);
                task_t *t = kthread_get_task(g_procs[i].tid);
                if (t) t->state = TASK_EXITED;
            }
            return 0;
        }
    }
    return (u64)-1;
}

static u64 sys_pipe(u64 pipefd_ptr, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    if (!valid_user_ptr(pipefd_ptr)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    int pipe_id = pipe_alloc();
    if (pipe_id == 0) return (u64)-1;
    int read_fd = proc_fd_alloc(proc);
    if (read_fd < 0) { g_pipes[pipe_id-1].in_use = 0; return (u64)-1; }
    proc->fds[read_fd].kind = 2;
    proc->fds[read_fd].pipe_id = pipe_id;
    int write_fd = proc_fd_alloc(proc);
    if (write_fd < 0) { proc->fds[read_fd].kind = 0; g_pipes[pipe_id-1].in_use = 0; return (u64)-1; }
    proc->fds[write_fd].kind = 3;
    proc->fds[write_fd].pipe_id = pipe_id;
    kernel_pipe_t *p = pipe_get(pipe_id);
    if (p) { p->reader_count = 1; p->writer_count = 1; }
    int *pipefd = (int*)(uintptr_t)pipefd_ptr;
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    return 0;
}

static u64 sys_read(u64 fd, u64 buf, u64 count, u64 a4) {
    (void)a4;
    if (!valid_user_buf(buf, count)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) return (u64)-1;
    if (pfd->kind == 1) return (u64)vfs_read(pfd->vfs_fd, (void*)(uintptr_t)buf, (int)count);
    if (pfd->kind == 2) {
        kernel_pipe_t *p = pipe_get(pfd->pipe_id);
        if (!p) return (u64)-1;
        while (pipe_data_avail(p) == 0) {
            if (p->writer_count == 0) return 0;
            p->reader_waiting = kthread_current_tid();
            kthread_block();
            p->reader_waiting = -1;
        }
        u32 avail = pipe_data_avail(p);
        if (avail > count) avail = (u32)count;
        u8 *dst = (u8*)(uintptr_t)buf;
        for (u32 i = 0; i < avail; i++)
            dst[i] = p->buf[(p->read_pos + i) % PIPE_BUF_SIZE];
        p->read_pos += avail;
        if (p->writer_waiting > 0) kthread_wake(p->writer_waiting);
        return (u64)avail;
    }
    return (u64)-1;
}

static u64 sys_write(u64 fd, u64 buf, u64 len, u64 a4) {
    (void)a4;
    if (!valid_user_buf(buf, len)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) {
        const char *p = (const char*)(uintptr_t)buf;
        for (u64 i = 0; i < len; i++) oc_console_putc(p[i]);
        return len;
    }
    if (fd == 1 || fd == 2) {
        const char *p = (const char*)(uintptr_t)buf;
        for (u64 i = 0; i < len; i++) oc_console_putc(p[i]);
        return len;
    }
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) return (u64)-1;
    if (pfd->kind == 1) return (u64)vfs_write(pfd->vfs_fd, (const void*)(uintptr_t)buf, (int)len);
    if (pfd->kind == 3) {
        kernel_pipe_t *p = pipe_get(pfd->pipe_id);
        if (!p) return (u64)-1;
        u32 remaining = (u32)len;
        u64 total_written = 0;
        const u8 *src = (const u8*)(uintptr_t)buf;
        while (remaining > 0) {
            while (pipe_space_avail(p) == 0) {
                if (p->reader_count == 0) return total_written > 0 ? total_written : (u64)-1;
                p->writer_waiting = kthread_current_tid();
                kthread_block();
                p->writer_waiting = -1;
            }
            u32 space = pipe_space_avail(p);
            u32 to_write = remaining < space ? remaining : space;
            for (u32 i = 0; i < to_write; i++)
                p->buf[(p->write_pos + i) % PIPE_BUF_SIZE] = src[total_written + i];
            p->write_pos += to_write;
            total_written += to_write;
            remaining -= to_write;
            if (p->reader_waiting > 0) kthread_wake(p->reader_waiting);
        }
        return total_written;
    }
    return (u64)-1;
}

static u64 sys_dup(u64 fd, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) return (u64)-1;
    int new_fd = proc_fd_alloc(proc);
    if (new_fd < 0) return (u64)-1;
    proc->fds[new_fd] = *pfd;
    if (pfd->kind == 2) { kernel_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->reader_count++; }
    else if (pfd->kind == 3) { kernel_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->writer_count++; }
    return (u64)new_fd;
}

static u64 sys_dup2(u64 oldfd, u64 newfd, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (oldfd >= PROC_MAX_FDS || newfd >= PROC_MAX_FDS) return (u64)-1;
    proc_fd_t *pfd = &proc->fds[oldfd];
    if (pfd->kind == 0) return (u64)-1;
    if (oldfd == newfd) return (u64)newfd;
    if (proc->fds[newfd].kind != 0) {
        if (proc->fds[newfd].kind == 2) { kernel_pipe_t *p = pipe_get(proc->fds[newfd].pipe_id); if (p) p->reader_count--; }
        else if (proc->fds[newfd].kind == 3) { kernel_pipe_t *p = pipe_get(proc->fds[newfd].pipe_id); if (p) p->writer_count--; }
        proc->fds[newfd].kind = 0;
    }
    proc->fds[newfd] = *pfd;
    if (pfd->kind == 2) { kernel_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->reader_count++; }
    else if (pfd->kind == 3) { kernel_pipe_t *p = pipe_get(pfd->pipe_id); if (p) p->writer_count++; }
    return (u64)newfd;
}

static u64 sys_mmap(u64 addr, u64 length, u64 prot, u64 a4) {
    (void)addr;(void)prot;(void)a4;
    if (length == 0) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    u64 pages = (length + 0xFFF) / 0x1000;
    /* BUG-010 FIX: Use per-process mmap_base instead of a global static.
     * The old `static u64 mmap_base` was shared across all processes,
     * causing cross-process memory corruption when multiple processes
     * called mmap. Now each process gets its own bump allocator. */
    if (proc->mmap_base == 0) proc->mmap_base = USER_MMAP_BASE;
    u64 result = proc->mmap_base;
    for (u64 i = 0; i < pages; i++) {
        u64 phys = pmm_alloc_frame();
        if (phys == 0) return (u64)-1;
        vmm_map_page(proc->as, proc->mmap_base + i * 0x1000, phys,
                     VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        oc_memset((void*)phys, 0, 0x1000);
    }
    proc->mmap_base += pages * 0x1000;
    return result;
}

static u64 sys_munmap(u64 addr, u64 length, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    u64 pages = (length + 0xFFF) / 0x1000;
    for (u64 i = 0; i < pages; i++) vmm_unmap_page(proc->as, addr + i * 0x1000);
    return 0;
}

static u64 sys_mprotect(u64 addr, u64 len, u64 prot, u64 a4) {
    (void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    u64 flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    if (prot & 2) flags |= VMM_FLAG_WRITE;
    if (!(prot & 4)) flags |= VMM_FLAG_NOEXEC;
    u64 pages = (len + 0xFFF) / 0x1000;
    for (u64 i = 0; i < pages; i++) vmm_protect_page(proc->as, addr + i * 0x1000, flags);
    return 0;
}

static u64 sys_brk(u64 addr, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (addr == 0) return proc->brk;
    if (addr < USER_BRK_BASE) return proc->brk;
    if (addr >= USER_MMAP_BASE) return proc->brk;
    u64 old_brk = proc->brk;
    u64 new_brk = (addr + 0xFFF) & ~0xFFFULL;
    if (new_brk > old_brk) {
        u64 vaddr = (old_brk + 0xFFF) & ~0xFFFULL;
        while (vaddr < new_brk) {
            u64 phys = pmm_alloc_frame();
            if (phys == 0) return proc->brk;
            vmm_map_page(proc->as, vaddr, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            oc_memset((void*)phys, 0, 0x1000);
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
    if (act_ptr && valid_user_ptr(act_ptr)) {
        sigaction_t *act = (sigaction_t*)(uintptr_t)act_ptr;
        p->sig_handlers[sig] = act->sa_handler;
    }
    if (oldact_ptr && valid_user_ptr(oldact_ptr)) {
        sigaction_t *oldact = (sigaction_t*)(uintptr_t)oldact_ptr;
        oldact->sa_handler = old;
        oldact->sa_flags = 0;
        oldact->sa_mask = 0;
    }
    return 0;
}

static u64 sys_sigreturn(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    return 0;
}

static u64 sys_chdir(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2;(void)a3;(void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    return (u64)shell_set_cwd((const char*)(uintptr_t)path);
}

static u64 sys_getcwd(u64 buf, u64 size, u64 a3, u64 a4) {
    (void)a3;(void)a4;
    if (!valid_user_buf(buf, size)) return (u64)-1;
    const char *cwd = shell_get_cwd();
    int len = oc_strlen(cwd);
    if (len >= (int)size) return (u64)-1;
    oc_memcpy((void*)(uintptr_t)buf, cwd, len + 1);
    return buf;
}

static u64 sys_ioctl(u64 fd, u64 cmd, u64 arg, u64 a4) {
    (void)fd;(void)cmd;(void)arg;(void)a4;
    return 0;
}

#define FD_SET_BYTES 128

static u64 sys_select(u64 nfds, u64 readfds_ptr, u64 timeout_ms, u64 a4) {
    (void)a4;
    if (!readfds_ptr || !valid_user_buf(readfds_ptr, FD_SET_BYTES)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    u8 *readfds = (u8*)(uintptr_t)readfds_ptr;
    extern u64 oc_timer_ticks(void);
    u64 start_ticks = oc_timer_ticks();
    u64 timeout_ticks = timeout_ms / 10;
    if (timeout_ticks == 0 && timeout_ms > 0) timeout_ticks = 1;
    tid_t my_tid = kthread_current_tid();

    for (;;) {
        int ready_count = 0;
        u8 result[FD_SET_BYTES];
        oc_memset(result, 0, FD_SET_BYTES);
        for (u64 fd = 0; fd < nfds && fd < PROC_MAX_FDS; fd++) {
            if (!(readfds[fd / 8] & (1 << (fd % 8)))) continue;
            proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 0) continue;
            int is_ready = 0;
            if (pfd->kind == 2) {
                kernel_pipe_t *p = pipe_get(pfd->pipe_id);
                if (p && (pipe_data_avail(p) > 0 || p->writer_count == 0)) is_ready = 1;
            } else if (pfd->kind == 1) is_ready = 1;
            if (is_ready) { result[fd / 8] |= (1 << (fd % 8)); ready_count++; }
        }
        if (ready_count > 0) {
            oc_memcpy(readfds, result, FD_SET_BYTES);
            return (u64)ready_count;
        }
        if (timeout_ms > 0) {
            u64 elapsed = oc_timer_ticks() - start_ticks;
            if (elapsed >= timeout_ticks) return 0;
        }
        for (u64 fd = 0; fd < nfds && fd < PROC_MAX_FDS; fd++) {
            if (!(readfds[fd / 8] & (1 << (fd % 8)))) continue;
            proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 2) {
                kernel_pipe_t *p = pipe_get(pfd->pipe_id);
                if (p) p->reader_waiting = my_tid;
            }
        }
        kthread_block();
        for (u64 fd = 0; fd < nfds && fd < PROC_MAX_FDS; fd++) {
            if (!(readfds[fd / 8] & (1 << (fd % 8)))) continue;
            proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 2) {
                kernel_pipe_t *p = pipe_get(pfd->pipe_id);
                if (p && p->reader_waiting == my_tid) p->reader_waiting = -1;
            }
        }
    }
}

typedef struct { int fd; short events; short revents; } pollfd_t;

static u64 sys_poll(u64 fds_ptr, u64 nfds, u64 timeout_ms, u64 a4) {
    (void)a4;
    if (!valid_user_buf(fds_ptr, nfds * sizeof(pollfd_t))) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    pollfd_t *pfds = (pollfd_t*)(uintptr_t)fds_ptr;
    extern u64 oc_timer_ticks(void);
    u64 start_ticks = oc_timer_ticks();
    for (;;) {
        int ready_count = 0;
        for (u64 i = 0; i < nfds; i++) {
            pfds[i].revents = 0;
            int fd = pfds[i].fd;
            if (fd < 0 || fd >= PROC_MAX_FDS) continue;
            proc_fd_t *pfd = &proc->fds[fd];
            if (pfd->kind == 0) continue;
            if (pfd->kind == 2) {
                kernel_pipe_t *p = pipe_get(pfd->pipe_id);
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
            u64 elapsed = oc_timer_ticks() - start_ticks;
            if (elapsed * 10 >= timeout_ms) return 0;
        }
        sched_yield();
    }
}

static u64 sys_execve(u64 path, u64 argv, u64 envp, u64 a4) {
    /* BUG-021 FIX: Add ALL embedded programs (was only 7 hardcoded names).
     * argv/envp are currently ignored — the kernel's ELF loader uses
     * embedded byte arrays, not VFS file loading. This is a design
     * limitation: to support arbitrary path-based exec, the kernel
     * would need vfs_open + read-ELF-from-disk support. For now,
     * all embedded programs are available via execve. */
    (void)argv;(void)envp;(void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    const char *name = (const char*)(uintptr_t)path;

    /* Strip /bin/ prefix if present */
    if (name[0] == '/' && name[1] == 'b' && name[2] == 'i' &&
        name[3] == 'n' && name[4] == '/') name += 5;

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
    extern const u8 userprog_mmap_multi[];
    extern const u8 userprog_ush[];
    const u8 *elf = NULL;
    if (oc_strcmp(name, "hello") == 0) elf = userprog_hello;
    else if (oc_strcmp(name, "fork_test") == 0) elf = userprog_fork_test;
    else if (oc_strcmp(name, "exec_test") == 0) elf = userprog_exec_test;
    else if (oc_strcmp(name, "pipe_test") == 0) elf = userprog_pipe_test;
    else if (oc_strcmp(name, "mmap_test") == 0) elf = userprog_mmap_test;
    else if (oc_strcmp(name, "signal_test") == 0) elf = userprog_signal_test;
    else if (oc_strcmp(name, "select_test") == 0) elf = userprog_select_test;
    else if (oc_strcmp(name, "dyn_hello") == 0) elf = userprog_dyn_hello;
    else if (oc_strcmp(name, "so_test") == 0) elf = userprog_so_test;
    else if (oc_strcmp(name, "dlsym_test") == 0) elf = userprog_dlsym_test;
    else if (oc_strcmp(name, "pie_test") == 0) elf = userprog_pie_test;
    else if (oc_strcmp(name, "reloc_test") == 0) elf = userprog_reloc_test;
    else if (oc_strcmp(name, "mmap_multi") == 0) elf = userprog_mmap_multi;
    else if (oc_strcmp(name, "ush") == 0 || oc_strcmp(name, "usershell") == 0)
        elf = userprog_ush;
    if (!elf) return (u64)-1;

    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;

    extern vmm_as_t vmm_kernel_as(void);
    vmm_as_t old_as = proc->as;
    __asm__ volatile("mov %0, %%cr3" :: "r"(vmm_kernel_as()) : "memory");
    if (old_as) vmm_destroy_address_space(old_as);
    proc->as = create_user_address_space();
    if (proc->as == 0) return (u64)-1;

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
    }
    u64 stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (u64 vaddr = stack_base; vaddr < USER_STACK_TOP; vaddr += 0x1000) {
        u64 phys = pmm_alloc_frame();
        if (phys) vmm_map_page(proc->as, vaddr, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    }
    proc->entry_point = hdr->entry;
    proc->user_rsp = USER_STACK_TOP - 16;
    proc->brk = USER_BRK_BASE;
    proc->is_fork_child = 0;
    kthread_set_cr3(proc->tid, proc->as);
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
 * Returns: base address of mapped .so, or 0 on failure.
 * Maps at proc->next_solib_addr (bump allocator from 0x50000000). */
static u64 sys_map_solib(u64 name_ptr, u64 name_len, u64 flags, u64 a4) {
    (void)flags; (void)a4;
    if (!valid_user_ptr(name_ptr) || name_len == 0 || name_len > 64)
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
            oc_strcmp(name, g_solib_table[j].name) == 0) {
            data = g_solib_table[j].data;
            size = g_solib_table[j].size;
            break;
        }
    }
    if (!data) return 0;
    /* Validate ELF. */
    if (size < 64) return 0;
    /* Use raw casts (elf64_hdr_t is defined in usermode.c, not exported). */
    const u8 *hdr = data;
    if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F')
        return 0;
    if (hdr[4] != 2) return 0;  /* ELF64 */
    u16 e_type = *(u16*)(data + 16);
    if (e_type != 3) return 0;  /* ET_DYN */
    u64 e_phoff = *(u64*)(data + 32);
    u16 e_phnum = *(u16*)(data + 56);
    /* Find current process. */
    user_proc_t *proc = user_process_current();
    if (!proc) return 0;
    u64 base = proc->next_solib_addr;
    /* Map each PT_LOAD segment at base + p_vaddr. */
    for (int k = 0; k < e_phnum; k++) {
        u32 p_type = *(u32*)(data + e_phoff + k * 56);
        if (p_type != 1) continue;  /* PT_LOAD */
        u64 p_offset = *(u64*)(data + e_phoff + k * 56 + 8);
        u64 p_vaddr = *(u64*)(data + e_phoff + k * 56 + 16);
        u64 p_filesz = *(u64*)(data + e_phoff + k * 56 + 32);
        u64 p_memsz = *(u64*)(data + e_phoff + k * 56 + 40);
        u64 load_vaddr = base + p_vaddr;
        if (map_user_pages(proc->as, load_vaddr,
                          data + p_offset, p_filesz) != 0)
            return 0;
        /* bss zero pages. */
        if (p_memsz > p_filesz) {
            u64 bs = load_vaddr + p_filesz;
            u64 be = bs + (p_memsz - p_filesz);
            u64 page = bs & ~0xFFFULL;
            while (page < be) {
                u64 phys;
                if (!vmm_is_mapped(proc->as, page, &phys)) {
                    phys = pmm_alloc_frame();
                    if (phys) {
                        vmm_map_page(proc->as, page, phys,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
                        oc_memset((void*)phys, 0, PMM_PAGE_SIZE);
                    }
                }
                page += PMM_PAGE_SIZE;
            }
        }
    }
    /* Advance bump allocator. */
    u64 max_end = 0;
    for (int k = 0; k < e_phnum; k++) {
        u32 p_type = *(u32*)(data + e_phoff + k * 56);
        if (p_type != 1) continue;
        u64 p_vaddr = *(u64*)(data + e_phoff + k * 56 + 16);
        u64 p_memsz = *(u64*)(data + e_phoff + k * 56 + 40);
        u64 end = p_vaddr + p_memsz;
        if (end > max_end) max_end = end;
    }
    proc->next_solib_addr = (base + max_end + 0xFFF) & ~0xFFFULL;
    return base;
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
void syscall_wp08a_init(void) {
    syscall_register(SYS_FORK, sys_fork);
    syscall_register(SYS_EXECVE, sys_execve);
    syscall_register(SYS_WAIT4, sys_wait4);
    syscall_register(SYS_KILL, sys_kill);
    syscall_register(SYS_GETPID, sys_getpid);
    syscall_register(SYS_GETPPID, sys_getppid);
    syscall_register(SYS_EXIT2, sys_exit2);
    syscall_register(SYS_PIPE, sys_pipe);
    syscall_register(SYS_DUP, sys_dup);
    syscall_register(SYS_DUP2, sys_dup2);
    syscall_register(SYS_MMAP, sys_mmap);
    syscall_register(SYS_MUNMAP, sys_munmap);
    syscall_register(SYS_MPROTECT, sys_mprotect);
    syscall_register(SYS_BRK, sys_brk);
    syscall_register(SYS_SIGNAL, sys_signal);
    syscall_register(SYS_SIGACTION, sys_sigaction);
    syscall_register(SYS_SIGRETURN, sys_sigreturn);
    syscall_register(SYS_CHDIR, sys_chdir);
    syscall_register(SYS_GETCWD, sys_getcwd);
    syscall_register(SYS_IOCTL, sys_ioctl);
    syscall_register(SYS_READ, sys_read);
    syscall_register(SYS_SELECT, sys_select);
    syscall_register(SYS_POLL, sys_poll);
    syscall_register(SYS_WRITE, sys_write);
    syscall_register(SYS_MAP_SOLIB, sys_map_solib);  /* WP-08b Batch 5 */
    /* WP-08cd: File operations + shell support */
    syscall_register(SYS_OPEN, sys_open);
    syscall_register(SYS_CLOSE, sys_close);
    syscall_register(SYS_STAT, sys_stat);
    syscall_register(SYS_READDIR, sys_readdir);
    syscall_register(SYS_MKDIR, sys_mkdir);
    syscall_register(SYS_RMDIR, sys_rmdir);
    syscall_register(SYS_UNLINK, sys_unlink);
    syscall_register(SYS_WRITE2, sys_write2);
    syscall_register(SYS_READLINE, sys_readline);
    syscall_register(SYS_GETCH, sys_getch);
    oc_memset(g_pipes, 0, sizeof(g_pipes));
}

/* ============================================================
 * WP-08cd: File operations + user-space shell support
 * ============================================================ */

/* SYS_OPEN(3): open a file path, return fd >= 0 or -1.
 * Maps to vfs_open. flags: 0=read, 1=write, 2=read+write. */
static u64 sys_open(u64 path, u64 flags, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    const char *p = (const char*)(uintptr_t)path;
    int vfs_fd = vfs_open(p, (int)flags);
    if (vfs_fd < 0) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    for (int i = 3; i < PROC_MAX_FDS; i++) {
        if (proc->fds[i].kind == 0) {
            proc->fds[i].kind = 1;
            proc->fds[i].vfs_fd = vfs_fd;
            return (u64)i;
        }
    }
    vfs_close(vfs_fd);
    return (u64)-1;  /* no free fd */
}

/* SYS_CLOSE(4): close a file descriptor. */
static u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) return (u64)-1;
    if (pfd->kind == 1) vfs_close(pfd->vfs_fd);
    /* kind 2/3 = pipe, just close the fd */
    pfd->kind = 0;
    return 0;
}

/* SYS_STAT(5): stat a file path. stat_buf points to a user buffer
 * of at least sizeof(vfs_stat_t) bytes. Returns 0 or -1. */
static u64 sys_stat(u64 path, u64 stat_buf, u64 a3, u64 a4) {
    (void)a3; (void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    if (!valid_user_ptr(stat_buf)) return (u64)-1;
    vfs_stat_t st;
    if (vfs_stat((const char*)(uintptr_t)path, &st) < 0) return (u64)-1;
    oc_memcpy((void*)(uintptr_t)stat_buf, &st, sizeof(st));
    return 0;
}

/* SYS_READDIR(6): read directory entry by index.
 * (path, index, dirent_buf) → 0 on success, -1 on end/error. */
static u64 sys_readdir(u64 path, u64 index, u64 dirent_buf, u64 a4) {
    (void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    if (!valid_user_ptr(dirent_buf)) return (u64)-1;
    vfs_dirent_t e;
    if (vfs_readdir((const char*)(uintptr_t)path, (int)index, &e) < 0)
        return (u64)-1;
    oc_memcpy((void*)(uintptr_t)dirent_buf, &e, sizeof(e));
    return 0;
}

/* SYS_MKDIR(7): create a directory. */
static u64 sys_mkdir(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    return (u64)vfs_mkdir((const char*)(uintptr_t)path);
}

/* SYS_RMDIR(8): remove a directory. */
static u64 sys_rmdir(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    return (u64)vfs_rmdir((const char*)(uintptr_t)path);
}

/* SYS_UNLINK(9): delete a file. */
static u64 sys_unlink(u64 path, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    if (!valid_user_ptr(path)) return (u64)-1;
    return (u64)vfs_unlink((const char*)(uintptr_t)path);
}

/* SYS_WRITE2(72): fd-aware write.
 * (fd, buf, len) → bytes written or -1.
 * fd=1 → console (stdout), fd=2 → console (stderr),
 * fd=pipe_write → pipe, fd=vfs → vfs_write. */
static u64 sys_write2(u64 fd, u64 buf, u64 len, u64 a4) {
    (void)a4;
    if (!valid_user_buf(buf, len)) return (u64)-1;
    user_proc_t *proc = user_process_current();
    if (!proc) return (u64)-1;
    if (fd >= PROC_MAX_FDS) return (u64)-1;
    proc_fd_t *pfd = &proc->fds[fd];
    if (pfd->kind == 0) {
        /* fd not open: check if it's stdout/stderr (1 or 2) */
        if (fd == 1 || fd == 2) {
            const char *p = (const char*)(uintptr_t)buf;
            for (u64 i = 0; i < len; i++) oc_console_putc(p[i]);
            return len;
        }
        return (u64)-1;
    }
    if (pfd->kind == 1) {
        /* VFS fd: write to file */
        return (u64)vfs_write(pfd->vfs_fd, (const void*)(uintptr_t)buf, (int)len);
    }
    if (pfd->kind == 3) {
        /* pipe-write fd */
        kernel_pipe_t *p = pipe_get(pfd->pipe_id);
        if (!p) return (u64)-1;
        const u8 *src = (const u8*)(uintptr_t)buf;
        u64 written = 0;
        while (written < len) {
            u32 space = pipe_space_avail(p);
            if (space == 0) {
                p->writer_waiting = kthread_current_tid();
                kthread_block();
                p->writer_waiting = -1;
                space = pipe_space_avail(p);
                if (space == 0) break;
            }
            u32 to_copy = (u32)((len - written < space) ? (len - written) : space);
            u32 wpos = p->write_pos % PIPE_BUF_SIZE;
            if (wpos + to_copy > PIPE_BUF_SIZE) to_copy = PIPE_BUF_SIZE - wpos;
            oc_memcpy(p->buf + wpos, src + written, to_copy);
            p->write_pos += to_copy;
            written += to_copy;
            if (p->reader_waiting >= 0) kthread_wake(p->reader_waiting);
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
    if (!valid_user_ptr(buf)) return (u64)-1;
    if (maxlen == 0) return 0;
    if (maxlen > 255) maxlen = 255;
    char line[256];
    int len = 0;
    for (;;) {
        /* Enable interrupts and halt until a key arrives */
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        int k = oc_keyboard_getch();
        if (k < 0) continue;
        if (k == 0x0A || k == 0x0D) {
            /* Enter: finish line */
            oc_console_putc('\n');
            break;
        }
        if (k == 0x08 || k == 0x7F) {
            /* Backspace */
            if (len > 0) {
                len--;
                oc_console_putc('\b');
                oc_console_putc(' ');
                oc_console_putc('\b');
            }
            continue;
        }
        if (k >= 0x20 && k < 0x7F && len < (int)maxlen - 1) {
            line[len++] = (char)k;
            oc_console_putc((char)k);
        }
    }
    line[len] = 0;
    oc_memcpy((void*)(uintptr_t)buf, line, (u64)len);
    ((char*)(uintptr_t)buf)[len] = 0;  /* null-terminate user buffer */
    return (u64)len;
}

/* SYS_GETCH(74): read one raw character from keyboard (no echo, no line edit).
 * Returns the ASCII key, or -1 if no key available. Non-blocking. */
static u64 sys_getch(u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1;(void)a2;(void)a3;(void)a4;
    int k = oc_keyboard_getch();
    if (k < 0) return (u64)-1;
    return (u64)k;
}
