/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-08a
 * File: kernel/ext_wp8a.c
 * Purpose: L1 extension API — int 0x80 syscall wrappers.
 */
#include "ext_wp8a.h"

pid_t proc_fork(void) { u64 r; __asm__ volatile("mov $10,%%rax; int $0x80":"=a"(r)::"rcx","r11","memory"); return (pid_t)r; }
int proc_exec(const char *p, const char *a[], const char *e[]) { u64 r; __asm__ volatile("mov $11,%%rax; mov %1,%%rdi; mov %2,%%rsi; mov %3,%%rdx; int $0x80":"=a"(r):"r"((u64)(uintptr_t)p),"r"((u64)(uintptr_t)a),"r"((u64)(uintptr_t)e):"rcx","r11","memory"); return (int)r; }
pid_t proc_wait(pid_t pid, int *s) { u64 r; __asm__ volatile("mov $12,%%rax; mov %1,%%rdi; mov %2,%%rsi; int $0x80":"=a"(r):"r"((u64)pid),"r"((u64)(uintptr_t)s):"rcx","r11","memory"); return (pid_t)r; }
void proc_exit(int c) { __asm__ volatile("mov $16,%%rax; mov %0,%%rdi; int $0x80"::"r"((u64)c):"rcx","r11","memory"); for(;;); }
pid_t proc_getpid(void) { u64 r; __asm__ volatile("mov $14,%%rax; int $0x80":"=a"(r)::"rcx","r11","memory"); return (pid_t)r; }
pid_t proc_getppid(void) { u64 r; __asm__ volatile("mov $15,%%rax; int $0x80":"=a"(r)::"rcx","r11","memory"); return (pid_t)r; }
int pipe_create(int pf[2]) { u64 r; __asm__ volatile("mov $20,%%rax; mov %1,%%rdi; int $0x80":"=a"(r):"r"((u64)(uintptr_t)pf):"rcx","r11","memory"); return (int)r; }
int fd_dup(int o) { u64 r; __asm__ volatile("mov $50,%%rax; mov %1,%%rdi; int $0x80":"=a"(r):"r"((u64)o):"rcx","r11","memory"); return (int)r; }
int fd_dup2(int o, int n) { u64 r; __asm__ volatile("mov $51,%%rax; mov %1,%%rdi; mov %2,%%rsi; int $0x80":"=a"(r):"r"((u64)o),"r"((u64)n):"rcx","r11","memory"); return (int)r; }
signal_handler_fn signal_register(int s, signal_handler_fn h) { u64 r; __asm__ volatile("mov $40,%%rax; mov %1,%%rdi; mov %2,%%rsi; int $0x80":"=a"(r):"r"((u64)s),"r"((u64)(uintptr_t)h):"rcx","r11","memory"); return (signal_handler_fn)r; }
int signal_send(pid_t p, int s) { u64 r; __asm__ volatile("mov $13,%%rax; mov %1,%%rdi; mov %2,%%rsi; int $0x80":"=a"(r):"r"((u64)p),"r"((u64)s):"rcx","r11","memory"); return (int)r; }
int signal_return(void) { u64 r; __asm__ volatile("mov $42,%%rax; int $0x80":"=a"(r)::"rcx","r11","memory"); return (int)r; }
void *sys_mmap(void *a, u64 l, int p) { u64 r; __asm__ volatile("mov $30,%%rax; mov %1,%%rdi; mov %2,%%rsi; mov %3,%%rdx; int $0x80":"=a"(r):"r"((u64)(uintptr_t)a),"r"(l),"r"((u64)p):"rcx","r11","memory"); return (void*)r; }
int sys_munmap(void *a, u64 l) { u64 r; __asm__ volatile("mov $31,%%rax; mov %1,%%rdi; mov %2,%%rsi; int $0x80":"=a"(r):"r"((u64)(uintptr_t)a),"r"(l):"rcx","r11","memory"); return (int)r; }
int sys_mprotect(void *a, u64 l, int p) { u64 r; __asm__ volatile("mov $32,%%rax; mov %1,%%rdi; mov %2,%%rsi; mov %3,%%rdx; int $0x80":"=a"(r):"r"((u64)(uintptr_t)a),"r"(l),"r"((u64)p):"rcx","r11","memory"); return (int)r; }
void *sys_brk(void *a) { u64 r; __asm__ volatile("mov $33,%%rax; mov %1,%%rdi; int $0x80":"=a"(r):"r"((u64)(uintptr_t)a):"rcx","r11","memory"); return (void*)r; }
int sys_chdir(const char *p) { u64 r; __asm__ volatile("mov $60,%%rax; mov %1,%%rdi; int $0x80":"=a"(r):"r"((u64)(uintptr_t)p):"rcx","r11","memory"); return (int)r; }
char *sys_getcwd(char *b, u64 s) { u64 r; __asm__ volatile("mov $61,%%rax; mov %1,%%rdi; mov %2,%%rsi; int $0x80":"=a"(r):"r"((u64)(uintptr_t)b),"r"(s):"rcx","r11","memory"); return (char*)r; }
int sys_ioctl(int f, u64 c, void *a) { u64 r; __asm__ volatile("mov $62,%%rax; mov %1,%%rdi; mov %2,%%rsi; mov %3,%%rdx; int $0x80":"=a"(r):"r"((u64)f),"r"(c),"r"((u64)(uintptr_t)a):"rcx","r11","memory"); return (int)r; }
int sys_select(int n, void *r, u32 t) { u64 v; __asm__ volatile("mov $80,%%rax; mov %1,%%rdi; mov %2,%%rsi; mov %3,%%rdx; int $0x80":"=a"(v):"r"((u64)n),"r"((u64)(uintptr_t)r),"r"((u64)t):"rcx","r11","memory"); return (int)v; }
int sys_poll(pollfd_t *f, u64 n, i64 t) { u64 r; __asm__ volatile("mov $81,%%rax; mov %1,%%rdi; mov %2,%%rsi; mov %3,%%rdx; int $0x80":"=a"(r):"r"((u64)(uintptr_t)f),"r"(n),"r"((u64)t):"rcx","r11","memory"); return (int)r; }
