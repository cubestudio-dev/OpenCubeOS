/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-08a
 * File: kernel/syscall.h
 * Purpose: Syscall number definitions, signal constants, and WP-08a API.
 *
 * Syscall ABI: rax=syscall_num, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4
 * Return: rax = result (>= 0 success, < 0 error)
 */
#ifndef OC_SYSCALL_H
#define OC_SYSCALL_H

#include "types.h"

/* Existing syscalls (WP-01..WP-07) */
#define SYS_EXIT           0
#define SYS_WRITE          1
#define SYS_WRITE_AND_EXIT 2

/* WP-08a: Process management */
#define SYS_FORK          10
#define SYS_EXECVE        11
#define SYS_WAIT4         12
#define SYS_KILL          13
#define SYS_GETPID        14
#define SYS_GETPPID       15
#define SYS_EXIT2         16

/* WP-08a: IPC */
#define SYS_PIPE          20
#define SYS_DUP           50
#define SYS_DUP2          51

/* WP-08a: Memory */
#define SYS_MMAP          30
#define SYS_MUNMAP        31
#define SYS_MPROTECT      32
#define SYS_BRK           33

/* WP-08a: Signals */
#define SYS_SIGNAL        40
#define SYS_SIGACTION     41
#define SYS_SIGRETURN     42

/* WP-08a: File / directory */
#define SYS_CHDIR         60
#define SYS_GETCWD        61
#define SYS_IOCTL         62
#define SYS_READ          70

/* WP-08a: I/O multiplexing */
#define SYS_SELECT        80
#define SYS_POLL          81

/* Signal numbers (POSIX subset) */
#define SIGHUP             1
#define SIGINT             2
#define SIGQUIT            3
#define SIGKILL            9
#define SIGSEGV           11
#define SIGTERM           15
#define SIGCHLD           17
#define SIGUSR1           10
#define SIGUSR2           12
#define NSIG              32

/* Signal handler type */
typedef void (*signal_handler_fn)(int sig);

/* sigaction structure (simplified POSIX) */
typedef struct {
    signal_handler_fn sa_handler;
    u64               sa_flags;
    u64               sa_mask;
} sigaction_t;

/* Initialize WP-08a syscalls (called from usermode_init) */
void syscall_wp08a_init(void);

/* WP-08b Batch 5: dynamic library loading via dlopen() */
#define SYS_MAP_SOLIB    90

#endif /* OC_SYSCALL_H */
