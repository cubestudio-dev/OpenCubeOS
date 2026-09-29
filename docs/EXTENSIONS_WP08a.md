<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# WP-08a L1 Extension Interfaces (Items 33-40)

**Work Package**: WP-08a — Complete syscall set (process & IPC)
**Total L1 Interfaces**: 8 (items 33-40)
**Status**: Complete
**License**: Apache 2.0

## Overview

WP-08a adds 8 L1 extension interfaces covering process management, IPC, signals, memory management, and I/O multiplexing. These interfaces wrap the kernel's syscall layer for use by L1 extensions.

## ABI Stability

All interfaces are stable. Signatures will not change in future versions.

---

## Interface 33: proc_fork

```c
pid_t proc_fork(void);
```

**Purpose**: Create a child process by duplicating the calling process's address space (POSIX fork).

**Returns**: 0 in the child, child's PID (>0) in the parent, -1 on failure.

**Example**:
```c
pid_t pid = proc_fork();
if (pid == 0) {
    // child code
    proc_exit(0);
}
// parent continues
```

---

## Interface 34: proc_exec / proc_wait / proc_exit

```c
int proc_exec(const char *path, const char *argv[], const char *envp[]);
pid_t proc_wait(pid_t pid, int *status);
void proc_exit(int code);
```

**Purpose**: Replace current process image (exec), wait for child (wait), terminate (exit).

**Parameters**:
- `proc_exec`: `path` = program path, `argv` = argument array, `envp` = environment (currently unused)
- `proc_wait`: `pid` = child PID to wait for, `status` = exit status output
- `proc_exit`: `code` = exit code

**Note**: `proc_exec` currently only recognizes hardcoded program names (hello, fork_test, etc.). Full VFS-based path loading is planned for a future WP.

---

## Interface 35: proc_getpid / proc_getppid

```c
pid_t proc_getpid(void);
pid_t proc_getppid(void);
```

**Purpose**: Get current process PID or parent PID.

**Returns**: PID (>0) or PPID (>0).

---

## Interface 36: pipe_create / fd_dup / fd_dup2

```c
int pipe_create(int pipefd[2]);
int fd_dup(int oldfd);
int fd_dup2(int oldfd, int newfd);
```

**Purpose**: Create a pipe (two fds), duplicate a fd, duplicate to specific fd.

**Returns**: 0 on success, -1 on failure. `pipe_create` fills `pipefd[0]` (read end) and `pipefd[1]` (write end).

---

## Interface 37: signal_register / signal_send / signal_return

```c
typedef void (*signal_handler_fn)(int sig);
signal_handler_fn signal_register(int sig, signal_handler_fn handler);
int signal_send(pid_t pid, int sig);
int signal_return(void);
```

**Purpose**: Register a signal handler, send a signal to a process, return from signal handler.

**Signals**: SIGKILL=9, SIGTERM=15, SIGUSR1=10, SIGUSR2=12, SIGCHLD=17.

---

## Interface 38: sys_mmap / sys_munmap / sys_mprotect / sys_brk

```c
void *sys_mmap(void *addr, u64 length, int prot);
int sys_munmap(void *addr, u64 length);
int sys_mprotect(void *addr, u64 length, int prot);
void *sys_brk(void *addr);
```

**Purpose**: Memory mapping, unmapping, protection, and heap break.

**Protection flags**: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4.

**Note**: `sys_mmap` uses per-process mmap_base (BUG-010 fix). `sys_brk` manages the process heap.

---

## Interface 39: sys_chdir / sys_getcwd

```c
int sys_chdir(const char *path);
char *sys_getcwd(char *buf, u64 size);
```

**Purpose**: Change current working directory, get current working directory.

---

## Interface 40: sys_ioctl / sys_select / sys_poll

```c
int sys_ioctl(int fd, u64 cmd, void *arg);
int sys_select(int nfds, void *readfds, u32 timeout_ms);
typedef struct { int fd; short events; short revents; } pollfd_t;
int sys_poll(pollfd_t *fds, u64 nfds, i64 timeout_ms);
```

**Purpose**: Device control, I/O multiplexing (select/poll).

**Note**: `sys_select` uses a simple readfds bitmap. `sys_poll` uses pollfd structures.

---

## Context Requirements (BUG-023)

These interfaces are designed for **user process context** only. When called from L1 kernel threads (not in a user process), they return 0 or -1 because `user_process_current()` is NULL. For L1 kernel extensions that need process management, use the underlying kernel APIs directly (`kthread_create`, `kthread_destroy`, etc.).

## Loading Model

WP-08a interfaces are available after `usermode_init()` is called during boot. They are registered as syscall handlers (int 0x80) and can be called from ring-3 user programs.
