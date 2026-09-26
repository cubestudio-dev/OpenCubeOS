/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-04
 * File: kernel/sync.h
 * Purpose: Synchronization primitives - spinlock, semaphore, mutex, condvar.
 */
#ifndef OC_SYNC_H
#define OC_SYNC_H

#include "types.h"
#include "sched.h"   /* for tid_t */

/* ---- Spinlock ---- */
typedef struct {
    volatile int locked;
    u64          lock_count;  /* total acquisitions */
    u64          _saved_flags; /* P2-46: saved RFLAGS for cli/sti */
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

/* ---- Mutex (with priority inheritance) ---- */
typedef struct {
    volatile int locked;
    tid_t        owner;       /* tid of the task holding the lock */
    int          owner_prio;  /* original priority of the owner (for PI) */
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
