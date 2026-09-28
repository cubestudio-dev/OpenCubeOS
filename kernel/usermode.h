/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04/WP-08a
 * File: kernel/usermode.h
 * Purpose: Userspace support - ring 3, syscalls, ELF loader, process table.
 */
#ifndef OC_USERMODE_H
#define OC_USERMODE_H

#include "types.h"
#include "vmm.h"
#include "sched.h"
#include "syscall.h"

/* Maximum user processes. */
#define MAX_USER_PROCS 16

/* User address space layout:
 *   0x400000    - code/data (ELF load address)
 *   0x500000    - brk heap start (grows up)
 *   0x3FFF0000  - user stack (grows down from 0x40000000)
 *   0x60000000  - mmap region (grows up from here)
 *
 * NOTE: The stack is in PDPT[0] (0-1GiB), which has a per-process PD.
 * This is critical for fork() — the child's copy of the stack must be
 * in the child's own page tables, not in a shared kernel PD.
 */
#define USER_CODE_BASE  0x400000ULL
#define USER_STACK_TOP  0x40000000ULL
#define USER_STACK_SIZE 0x10000ULL  /* 64 KiB */
#define USER_BRK_BASE   0x500000ULL
#define USER_MMAP_BASE  0x3C000000ULL

/* WP-08b Batch 5: dynamic linking address space layout:
 *   0x08000000  - ld.so API table (R+W page, 1 page)
 *   0x10000000  - ld.so itself (R+E, fixed)
 *   0x20000000  - main program ELF (mapped by kernel)
 *   0x30000000  - pre-mapped libfoo.so (for DT_NEEDED case)
 *   0x38000000  - dlopen'd .so bump allocator (per-process)
 *   0x3C000000  - mmap base (per-process)
 * All addresses below 0x40000000 (1GB) are in PDPT[0], which is
 * per-process (each process has its own PDPT[0]). Addresses above
 * 1GB use shared kernel page tables and are NOT isolated per-process.
 * BUG-010 FIX: moved from 0x50000000/0x60000000 (above 1GB, shared)
 * to 0x38000000/0x3C000000 (below 1GB, per-process). */
#define LDSO_API_TABLE_ADDR 0x08000000ULL
#define USER_SOLIB_BASE     0x38000000ULL

/* Maximum FDs per process (includes VFS fds + pipe fds) */
#define PROC_MAX_FDS 32

/* PID type. */
typedef int pid_t;

/* Per-process file descriptor entry.
 * kind: 0=free, 1=VFS fd, 2=pipe-read, 3=pipe-write */
typedef struct {
    int kind;
    int vfs_fd;
    int pipe_id;
} proc_fd_t;

/* User process structure. */
typedef struct user_proc {
    pid_t      pid;
    pid_t      ppid;
    tid_t      tid;
    tid_t      parent_tid;
    vmm_as_t   as;
    u64        entry_point;
    u64        user_rsp;
    u64        brk;
    int        alive;
    int        exit_code;
    int        waited;
    int        pending_signal;
    char       name[32];

    /* WP-08a: per-process file descriptor table */
    proc_fd_t  fds[PROC_MAX_FDS];

    /* WP-08a: signal handlers */
    signal_handler_fn sig_handlers[NSIG];

    /* P3-12: Saved user-mode register frame for sigreturn.
     * When a signal is delivered (in syscall_dispatch, after the syscall
     * returns but before iretq), the kernel saves the current IRQ frame
     * here, then modifies the live frame to call the user's handler.
     * When the handler invokes SYS_SIGRETURN (42), the kernel copies
     * this saved frame back into the live frame so iretq restores the
     * pre-signal RIP/RSP/RFLAGS/registers.
     * sig_in_progress: 1 = saved frame is valid (next sigreturn restores it). */
    u64 sig_saved_frame[22];
    int sig_in_progress;

    /* WP-08a: fork-child state. When is_fork_child=1, the launcher
     * enters ring 3 at fork_rip/fork_rsp with fork_rax in RAX,
     * implementing true POSIX fork semantics (child continues from
     * the fork() call point). */
    int        is_fork_child;
    u64        fork_rip;
    u64        fork_rsp;
    u64        fork_rflags;
    u64        fork_rax;
    u64        fork_regs[22];  /* WP-08cd: full saved register array (22 u64) */

    /* WP-08b Batch 5: per-process bump allocator for dlopen'd .so files.
     * Starts at USER_SOLIB_BASE (0x38000000) and grows up. */
    u64        next_solib_addr;

    /* BUG-010 FIX: per-process mmap base (was global static, causing
     * cross-process memory corruption). Initialized to USER_MMAP_BASE
     * on first mmap call. */
    u64        mmap_base;
} user_proc_t;

/* Syscall handler function type. */
typedef u64 (*syscall_handler_fn)(u64 arg1, u64 arg2, u64 arg3, u64 arg4);

/* WP-08a: exposed for syscall.c */
vmm_as_t create_user_address_space(void);
int map_user_pages(vmm_as_t as, u64 vaddr, const void *src, u64 size);
void user_task_launcher(void *arg);

/* WP-08a: global process table (used by syscall.c) */
extern user_proc_t g_procs[MAX_USER_PROCS];

/* Copy all user-accessible pages from src_as to a new address space. */
vmm_as_t copy_user_address_space(vmm_as_t src_as);

/* Initialize userspace subsystem. */
void usermode_init(void);

/* Create a user process from an ELF binary. */
pid_t user_process_create(const u8 *elf_data, u64 elf_size, const char *name);

/* Kill a user process. */
int user_process_kill(pid_t pid);

/* List all user processes. */
void user_process_list(void);

/* Register a syscall handler. */
int syscall_register(int num, syscall_handler_fn handler);

/* Default syscall handlers. */
u64 syscall_write(u64 buf, u64 len, u64 arg3, u64 arg4);
u64 syscall_exit(u64 code, u64 arg2, u64 arg3, u64 arg4);

/* Called from the int 0x80 entry stub. */
void syscall_dispatch(u64 *regs);

/* Enter ring 3 for the first time. */
void enter_ring3(u64 entry_point, u64 user_rsp, u64 user_cr3);

/* WP-08a: Enter ring 3 with a saved fork frame (pure assembly).
 * Arguments: rdi=rip, rsi=rsp, rdx=rflags, rcx=rax, r8=cr3 */
void enter_ring3_fork(u64 *frame, u64 cr3);

/* WP-08a: Get the current process. */
user_proc_t *user_process_current(void);

/* WP-08a: Save/restore the current interrupt frame (for fork). */
void user_set_current_frame(u64 *frame_regs);
u64 *user_get_current_frame(void);

/* P3-11: Unified resource reaper — called from sys_exit2, sys_kill,
 * and exception kill. Closes all fds + destroys the user AS. */
void user_process_reap_resources(user_proc_t *p, int exit_code);

#endif /* OC_USERMODE_H */
