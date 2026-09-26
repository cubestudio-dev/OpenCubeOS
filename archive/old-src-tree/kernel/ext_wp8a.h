/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-08a
 * File: kernel/ext_wp8a.h
 * Purpose: L1 EXTENSION API - WP-08a interfaces (items 33-40).
 */
#ifndef OC_EXT_WP8A_H
#define OC_EXT_WP8A_H

#include "types.h"
#include "syscall.h"
#include "usermode.h"

#ifdef __cplusplus
extern "C" {
#endif

pid_t proc_fork(void);
int proc_exec(const char *path, const char *argv[], const char *envp[]);
pid_t proc_wait(pid_t pid, int *status);
void proc_exit(int code) __attribute__((noreturn));
pid_t proc_getpid(void);
pid_t proc_getppid(void);
int pipe_create(int pipefd[2]);
int fd_dup(int oldfd);
int fd_dup2(int oldfd, int newfd);
signal_handler_fn signal_register(int sig, signal_handler_fn handler);
int signal_send(pid_t pid, int sig);
int signal_return(void);
void *sys_mmap(void *addr, u64 length, int prot);
int sys_munmap(void *addr, u64 length);
int sys_mprotect(void *addr, u64 length, int prot);
void *sys_brk(void *addr);
int sys_chdir(const char *path);
char *sys_getcwd(char *buf, u64 size);
int sys_ioctl(int fd, u64 cmd, void *arg);
int sys_select(int nfds, void *readfds, u32 timeout_ms);
typedef struct { int fd; short events; short revents; } pollfd_t;
int sys_poll(pollfd_t *fds, u64 nfds, i64 timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* OC_EXT_WP8A_H */
