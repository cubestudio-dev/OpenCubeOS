/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04
 * File: kernel/sched.h
 * Purpose: Preemptive scheduler - task control blocks, priority scheduling,
 *          time-slice round-robin, context switching.
 */
#ifndef OC_SCHED_H
#define OC_SCHED_H

#include "types.h"

/* Task states. */
#define TASK_READY    0
#define TASK_RUNNING  1
#define TASK_BLOCKED  2
#define TASK_EXITED   3

/* Priority levels: 0 = highest, 31 = lowest. Default = 15. */
#define TASK_PRIO_MIN  0
#define TASK_PRIO_MAX  31
#define TASK_PRIO_DEFAULT 15

/* Maximum number of tasks. */
#define MAX_TASKS 32

/* Time slice in ticks (at 100 Hz, 1 tick = 10ms). */
#define TIME_SLICE_TICKS 2  /* 20ms per slice */

/* WP-09-FIX BUG-008: starvation guard. After this many consecutive ticks
 * a running task is force-switched out in favour of the lowest-priority
 * ready task (100 ticks = 1s at the 100 Hz timer). */
#define SCHED_STARVE_LIMIT 100

/* Task ID. 0 = kernel/idle task. */
typedef int tid_t;

/* Task control block. */
typedef struct task {
    tid_t  tid;
    char   name[32];
    int    state;
    int    priority;
    u64    cpu_time_ticks;   /* total CPU time consumed */
    u64    switch_count;     /* how many times this task was switched to */
    u64    ticks_remaining;  /* time slice remaining */

    /* Saved register context for context switch.
     * The order MUST match context_switch.S. */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;

    /* Stack for kernel threads. */
    u64 *stack_base;     /* allocated stack base */
    u64  stack_size;

    /* Address space (CR3) for this task. 0 = kernel identity mapping. */
    u64 cr3;

    /* BUG-029 FIX: RSP0 for TSS (kernel stack top for ring-3 IRQ entry).
     * Set when a user process is created; updated on context switch. */
    u64 rsp0;

    /* Function entry point and argument for new tasks. */
    void (*entry)(void *arg);
    void *arg;

    int in_use;
} task_t;

/* Initialize the scheduler. Called from kmain. */
void sched_init(void);

/* Create a kernel thread. Returns tid >= 0, or -1 on failure. */
tid_t kthread_create(void (*fn)(void *arg), void *arg, const char *name, int prio);

/* P4 fix: Register a callback invoked when a tid is destroyed.
 * Lets subsystems with tid waiters (sync.c's semaphores, pipe wait
 * queues) remove the dead tid from their waiter lists, preventing
 * slot leaks. The hook is called with interrupts OFF inside
 * kthread_destroy, so the hook must NOT call kthread_block/yield. */
typedef void (*tid_destroyed_hook_fn)(tid_t tid);
void kthread_register_destroyed_hook(tid_destroyed_hook_fn fn);

/* Destroy a task. Returns 0 on success. */
int kthread_destroy(tid_t tid);

/* BUG-006 FIX: Set a task's priority (0=highest..31=lowest). */
int kthread_set_priority(tid_t tid, int priority);

/* Block the current task. Returns 0 on success. */
int kthread_block(void);

/* Wake a blocked task. Returns 0 on success. */
int kthread_wake(tid_t tid);

/* Get the current task. Returns NULL if scheduler not yet initialized. */
task_t *kthread_current(void);

/* Get the current task's tid. Returns 0 for the idle/kernel task. */
tid_t kthread_current_tid(void);

/* List all tasks (for the `ps` command). Prints to console. */
void kthread_list(void);

/* Called from the timer IRQ to drive the scheduler. */
void sched_tick(void);

/* WP-09-FIX BUG-004/BUG-007: mark a task EXITED and remove it from the
 * ready queue so the scheduler can never resurrect it (sched_switch_to
 * sets state=TASK_RUNNING on the next pop). Its kernel stack is freed
 * later by sched_reap_exited(). Safe to call from another task's
 * context (e.g. the shell running `kill`). */
void sched_task_exited(tid_t tid);

/* Called to yield the CPU voluntarily. */
void sched_yield(void);

/* Scheduler statistics. */
typedef struct sched_stats {
    u64 total_switches;
    u64 total_preemptions;
    u64 current_tid;
    int  active_tasks;
} sched_stats_t;

void sched_get_stats(sched_stats_t *out);

/* Context switch assembly routine (in context_switch.S).
 * Saves current regs to old->..., loads new->... regs, jumps to new->rip. */
void oc_context_switch(task_t *old_task, task_t *new_task);

/* WP-04: Set a task's CR3 (address space). Used by usermode.c. */
void kthread_set_cr3(tid_t tid, u64 cr3);

/* WP-04: Get a task pointer by tid (for external access). */
task_t *kthread_get_task(tid_t tid);

#endif /* OC_SCHED_H */
