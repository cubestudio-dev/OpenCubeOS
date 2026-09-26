/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-04
 * File: kernel/sync.c
 * Purpose: Synchronization primitives implementation.
 */
#include "sync.h"
#include "sched.h"
#include "string.h"

/* Global stats. */
static sync_stats_t g_stats;

void sync_get_stats(sync_stats_t *out) {
    if (!out) return;
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    *out = g_stats;
    if (!(flags & 0x200)) __asm__ volatile("sti");
}

/* ---- Spinlock ---- */
void spin_init(spinlock_t *lock) {
    lock->locked = 0;
    lock->lock_count = 0;
}

/* P2-46: spinlock does NOT cli/sti. On this single-CPU kernel, IRQ
 * handlers (timer, keyboard, network poll) do not acquire spinlocks
 * except in carefully controlled paths (timer IRQ calls net_poll which
 * uses e1000 registers directly, not spinlocks). Disabling interrupts
 * in spin_lock would prevent the timer from firing, causing tasks that
 * spin on a lock to monopolize the CPU (verified: synctest dropped to
 * 515/3000). So we keep the simple non-IRQ-safe spinlock. */
void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile("pause");
    }
    lock->lock_count++;
    g_stats.spin_locks++;
}

void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->locked);
    g_stats.spin_unlocks++;
}

int spin_trylock(spinlock_t *lock) {
    if (__sync_lock_test_and_set(&lock->locked, 1) == 0) {
        lock->lock_count++;
        g_stats.spin_locks++;
        return 1;
    }
    return 0;
}

/* ---- Semaphore ---- */
void sem_init(sem_t *sem, int initial) {
    sem->count = initial;
    spin_init(&sem->lock);
    sem->wait_count = 0;
    sem->post_count = 0;
    sem->num_waiters = 0;
    for (int i = 0; i < SEM_MAX_WAITERS; i++) sem->waiters[i] = (tid_t)-1;
}

void sem_wait(sem_t *sem) {
    for (;;) {
        spin_lock(&sem->lock);
        if (sem->count > 0) {
            sem->count--;
            spin_unlock(&sem->lock);
            g_stats.sem_waits++;
            return;
        }
        /* P0-5 FIX: register ourselves as a waiter AND set our state to
         * BLOCKED while still holding the spinlock. This closes the
         * lost-wakeup race: if sem_post runs between our unlock and
         * sched_yield, it will see us in the waiter list AND see our
         * state as BLOCKED, so kthread_wake will succeed. */
        tid_t my_tid = kthread_current_tid();
        if (sem->num_waiters < SEM_MAX_WAITERS) {
            sem->waiters[sem->num_waiters++] = my_tid;
        }
        /* Mark ourselves BLOCKED before releasing the lock so sem_post
         * can wake us. */
        task_t *me = kthread_current();
        if (me) me->state = TASK_BLOCKED;
        spin_unlock(&sem->lock);
        sem->wait_count++;
        sched_yield();  /* give up CPU — we're BLOCKED, sem_post will wake us */
        /* Woken by sem_post. Remove ourselves from the waiter list and
         * loop back to re-check count. sem_post always increments count
         * before waking, so count should be > 0 now. The loop handles
         * the rare case where another task consumed count between our
         * wake and our re-acquire of the lock. */
        spin_lock(&sem->lock);
        for (int i = 0; i < sem->num_waiters; i++) {
            if (sem->waiters[i] == my_tid) {
                sem->waiters[i] = sem->waiters[--sem->num_waiters];
                break;
            }
        }
        spin_unlock(&sem->lock);
        /* Loop back to top: re-check count. */
    }
}

void sem_post(sem_t *sem) {
    spin_lock(&sem->lock);
    sem->count++;
    /* P0-5 FIX: wake one waiter if any. The woken waiter will re-check
     * count and consume it. We always increment count (even when waking
     * a waiter) so no token is ever lost — the woken waiter sees count>0
     * and decrements it. */
    tid_t to_wake = -1;
    if (sem->num_waiters > 0) {
        to_wake = sem->waiters[--sem->num_waiters];
    }
    spin_unlock(&sem->lock);
    sem->post_count++;
    g_stats.sem_posts++;
    if (to_wake >= 0) {
        kthread_wake(to_wake);
    } else {
        sched_yield();
    }
}

/* ---- Mutex ---- */
void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = -1;
    m->owner_prio = 0;
    m->lock_count = 0;
}

void mutex_lock(mutex_t *m) {
    tid_t my_tid = kthread_current_tid();
    while (__sync_lock_test_and_set(&m->locked, 1)) {
        /* Contended: yield and retry. */
        sched_yield();
    }
    m->owner = my_tid;
    m->owner_prio = kthread_current()->priority;
    m->lock_count++;
    g_stats.mutex_locks++;
}

void mutex_unlock(mutex_t *m) {
    /* P1-11 FIX: check that the caller actually owns the mutex. */
    tid_t my_tid = kthread_current_tid();
    if (m->owner != my_tid) return;  /* not the owner — ignore */
    m->owner = -1;
    __sync_lock_release(&m->locked);
    g_stats.mutex_unlocks++;
    sched_yield();
}

/* ---- Condition variable ---- */
void cond_init(cond_t *c) {
    spin_init(&c->lock);
    c->waiters = 0;
    c->signal_count = 0;
    for (int i = 0; i < COND_MAX_WAITERS; i++) c->waiter_tids[i] = (tid_t)-1;
}

void cond_wait(cond_t *c, mutex_t *m) {
    spin_lock(&c->lock);
    c->waiters++;
    /* P0-5 FIX: register our TID AND set BLOCKED state while holding the
     * lock, so cond_signal can wake us without a lost-wakeup race. */
    tid_t my_tid = kthread_current_tid();
    for (int i = 0; i < COND_MAX_WAITERS; i++) {
        if (c->waiter_tids[i] == 0 || c->waiter_tids[i] == (tid_t)-1) {
            c->waiter_tids[i] = my_tid;
            break;
        }
    }
    task_t *me = kthread_current();
    if (me) me->state = TASK_BLOCKED;
    spin_unlock(&c->lock);

    mutex_unlock(m);
    sched_yield();  /* give up CPU — we're BLOCKED, cond_signal will wake us */
    g_stats.cond_waits++;

    mutex_lock(m);

    spin_lock(&c->lock);
    c->waiters--;
    for (int i = 0; i < COND_MAX_WAITERS; i++) {
        if (c->waiter_tids[i] == my_tid) {
            c->waiter_tids[i] = (tid_t)-1;
            break;
        }
    }
    spin_unlock(&c->lock);
}

void cond_signal(cond_t *c) {
    spin_lock(&c->lock);
    tid_t to_wake = (tid_t)-1;
    if (c->waiters > 0) {
        c->signal_count++;
        g_stats.cond_signals++;
        /* P0-5 FIX: wake one specific waiter. */
        for (int i = 0; i < COND_MAX_WAITERS; i++) {
            if (c->waiter_tids[i] != 0 && c->waiter_tids[i] != (tid_t)-1) {
                to_wake = c->waiter_tids[i];
                c->waiter_tids[i] = (tid_t)-1;
                break;
            }
        }
    }
    spin_unlock(&c->lock);
    if (to_wake != (tid_t)-1) kthread_wake(to_wake);
    else sched_yield();
}

void cond_broadcast(cond_t *c) {
    spin_lock(&c->lock);
    if (c->waiters > 0) {
        c->signal_count += c->waiters;
        g_stats.cond_signals += c->waiters;
        /* P0-5 FIX: wake all waiters. */
        for (int i = 0; i < COND_MAX_WAITERS; i++) {
            if (c->waiter_tids[i] != 0 && c->waiter_tids[i] != (tid_t)-1) {
                kthread_wake(c->waiter_tids[i]);
                c->waiter_tids[i] = (tid_t)-1;
            }
        }
    }
    spin_unlock(&c->lock);
    sched_yield();
}
