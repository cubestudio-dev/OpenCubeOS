/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04
 * File: kernel/sync.h
 * Purpose: Synchronization primitives - spinlock, semaphore, mutex, condvar.
 */
#ifndef OC_SYNC_H
#define OC_SYNC_H

#include "types.h"
#include "core_sched.h"   /* for tid_t */

/* ---- Spinlock ----
 * BUG-0285 (A3-08): irqsave/irqrestore semantics. spin_lock records the
 * caller's RFLAGS.IF in _saved_flags (the slot P2-46 reserved for
 * exactly this) and runs the critical section with IF=0; spin_unlock
 * releases the lock FIRST and only then restores IF, so on this single
 * CPU a lock is never observed held by a context that can be preempted
 * while holding it: an IRQ handler can only run when no irqsave holder
 * is inside its critical section, so it can never spin on a holder
 * that cannot make progress. The remaining contract is unchanged and
 * is enforced by review, not hardware: holders must not sleep, yield
 * or block inside a spinlock critical section (every in-tree caller -
 * core_sync.c sem/cond windows and main.c synctest - satisfies this). */
typedef struct {
    volatile int locked;
    u64          lock_count;  /* total acquisitions */
    u64          _saved_flags; /* irqsave: caller's RFLAGS at acquire */
} spinlock_t;

void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int  spin_trylock(spinlock_t *lock);

/* ---- Semaphore ---- */
#define SEM_MAX_WAITERS 16
typedef struct {
    volatile int count;
    spinlock_t   lock;
    u64          wait_count;
    u64          post_count;
    tid_t        waiters[SEM_MAX_WAITERS];  /* P0-5 FIX: track blocked TIDs */
    int          num_waiters;
} sem_t;

void sem_init(sem_t *sem, int initial);
void sem_wait(sem_t *sem);
void sem_post(sem_t *sem);

/* ---- Mutex ----
 * BUG-0286 (A3-09): FIFO waiter management, NO priority inheritance.
 * The header used to claim "with priority inheritance" while owner_prio
 * was written and never read - no PI logic ever existed. That claim is
 * gone. The real semantics on this single-CPU, fixed-priority kernel:
 * contended lockers register in m->waiters[] in arrival order and BLOCK
 * (register + BLOCKED inside one cli window, the same lost-wakeup shape
 * as sem_wait's P0-5 fix; acquire + owner-registration is likewise one
 * cli window, and mutex_unlock clears the owner + releases inside one).
 * mutex_unlock wakes the FIFO head, sweeping waiters whose tid died
 * while queued. A holder killed while HOLDING the mutex is force-
 * released by the next contended mutex_lock (dead-owner check) - the
 * lock object cannot wedge forever, although the dead holder's critical
 * section is not rolled back, exactly like a POSIX mutex held by a
 * cancelled thread. A woken waiter re-contends through the normal
 * acquire path; a higher-priority locker may still barge ahead of it
 * between release and re-acquire - that is documented unfairness, not
 * priority inheritance. There is deliberately no PI: single CPU,
 * fixed priorities, no RT workload (see findings A3-09). */
#define MUTEX_MAX_WAITERS 16
typedef struct {
    volatile int locked;
    tid_t        owner;       /* tid of the task holding the lock */
    tid_t        waiters[MUTEX_MAX_WAITERS];  /* FIFO queue of blocked tids */
    int          num_waiters;
    u64          lock_count;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* ---- Condition variable ---- */
#define COND_MAX_WAITERS 16
typedef struct {
    spinlock_t   lock;
    int          waiters;
    u64          signal_count;
    tid_t        waiter_tids[COND_MAX_WAITERS];  /* P0-5 FIX: track blocked TIDs */
} cond_t;

void cond_init(cond_t *c);
void cond_wait(cond_t *c, mutex_t *m);
void cond_signal(cond_t *c);
void cond_broadcast(cond_t *c);

/* ---- Sync stats ---- */
typedef struct sync_stats {
    u64 spin_locks;
    u64 spin_unlocks;
    u64 sem_waits;
    u64 sem_posts;
    u64 mutex_locks;
    u64 mutex_unlocks;
    u64 cond_waits;
    u64 cond_signals;
} sync_stats_t;

void sync_get_stats(sync_stats_t *out);

#endif /* OC_SYNC_H */
