/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04
 * File: kernel/sync.c
 * Purpose: Synchronization primitives implementation.
 */
#include "core_sync.h"
#include "core_sched.h"
#include "lib_string.h"

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
    lock->_saved_flags = 0;
}

/* BUG-0285 (A3-08) FIX: spinlocks are now IRQ-safe (irqsave/irqrestore
 * semantics). The old P2-46 decision left the lock non-IRQ-safe with
 * only a comment as protection: on this single-CPU kernel, if an IRQ
 * handler ever acquired a spinlock held by the interrupted context, the
 * handler would spin forever on a holder that can no longer run - an
 * unpreemptible deadlock with no assertion to catch it (verified: the
 * timer -> net_poll path does not take these locks today, but nothing
 * enforced that invariant). Now spin_lock saves RFLAGS and clears IF
 * before the test-and-set, and spin_unlock releases the lock FIRST and
 * only then restores the IF state recorded at acquisition time (the
 * _saved_flags slot reserved by P2-46). Release-before-restore is what
 * makes the scheme correct on one CPU: at the instant IF comes back the
 * lock is already free, so a preemption can never observe a held lock
 * whose holder is no longer running. A holder can no longer be
 * interrupted, so an IRQ can never contend with its own holder; timer
 * delivery resumes at spin_unlock. Holders must still not sleep/yield
 * inside a spinlock critical section (unchanged contract; verified for
 * every in-tree caller: core_sync.c sem/cond windows and main.c
 * synctest never yield while holding). Recursive spin_lock on a lock
 * the caller already holds remains a programming error (self-deadlock),
 * as before. */
void spin_lock(spinlock_t *lock) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile("pause");
    }
    lock->_saved_flags = flags;
    lock->lock_count++;
    g_stats.spin_locks++;
}

void spin_unlock(spinlock_t *lock) {
    u64 flags = lock->_saved_flags;
    __sync_lock_release(&lock->locked);
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
    g_stats.spin_unlocks++;
}

int spin_trylock(spinlock_t *lock) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    if (__sync_lock_test_and_set(&lock->locked, 1) == 0) {
        lock->_saved_flags = flags;
        lock->lock_count++;
        g_stats.spin_locks++;
        return 1;
    }
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
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
         * core_sched_yield, it will see us in the waiter list AND see our
         * state as BLOCKED, so core_kthread_wake will succeed. Under the
         * BUG-0285 irqsave spinlock the whole critical section is also
         * non-interruptible, which only strengthens that guarantee. */
        tid_t my_tid = core_kthread_current_tid();
        if (sem->num_waiters < SEM_MAX_WAITERS) {
            sem->waiters[sem->num_waiters++] = my_tid;
        } else {
            /* BUG-046 FIX: Waiter table is full. Don't block — if we
             * set state to BLOCKED without being in the waiter list,
             * sem_post can never wake us → permanent deadlock.
             * Instead, release the lock, spin briefly, and retry. */
            spin_unlock(&sem->lock);
            for (volatile int spin = 0; spin < 1000; spin++);
            /* BUG-0104 FIX: the fallback used to re-take the spinlock
             * HERE and then `continue` into the loop head which takes
             * the same spinlock again. A non-recursive spinlock does
             * not permit that: the second spin_lock spun forever on a
             * lock WE hold — a self-deadlock on the table-full path.
             * Leave the lock free; the loop head re-locks it. */
            continue;  /* re-check count */
        }
        /* Mark ourselves BLOCKED before releasing the lock so sem_post
         * can wake us. */
        task_t *me = core_kthread_current();
        if (me) me->state = TASK_BLOCKED;
        spin_unlock(&sem->lock);
        sem->wait_count++;
        core_sched_yield();  /* give up CPU — we're BLOCKED, sem_post will wake us */
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
    /* P4 fix: scan the waiter list, removing dead tids (state != BLOCKED
     * means core_kthread_wake would fail anyway — they were destroyed while
     * waiting). Old code popped waiters[0] unconditionally; if that tid
     * was dead, core_kthread_wake returned -1 and the wake was lost (the token
     * stayed in count but no real waiter was woken). Now we skip dead
     * waiters and wake the first live one. */
    while (sem->num_waiters > 0) {
        /* BUG-020 FIX: Wake in FIFO order (waiters[0] is the oldest). */
        tid_t cand = sem->waiters[0];
        /* Shift the queue down (remove waiters[0]). */
        for (int i = 1; i < sem->num_waiters; i++)
            sem->waiters[i - 1] = sem->waiters[i];
        sem->num_waiters--;
        /* Check if this tid is still a valid blocked task.
         * core_kthread_wake returns -1 if state != BLOCKED (incl. EXITED).
         * We can't call core_kthread_wake here while holding the spinlock
         * (core_kthread_wake can take the ready-queue lock). So we just
         * try the wake after unlocking — and if it fails, we retry
         * the loop to find the next live waiter.
         * To keep this simple, we check task state via core_kthread_get_task. */
        extern task_t *core_kthread_get_task(tid_t);  /* sched.h */
        task_t *t = core_kthread_get_task(cand);
        if (t && t->state == TASK_BLOCKED) {
            to_wake = cand;
            break;
        }
        /* Else: dead tid, continue scanning. */
    }
    spin_unlock(&sem->lock);
    sem->post_count++;
    g_stats.sem_posts++;
    if (to_wake >= 0) {
        core_kthread_wake(to_wake);
    } else {
        core_sched_yield();
    }
}

/* ---- Mutex ----
 * BUG-0286 (A3-09) FIX: real FIFO waiter management. The old lock was a
 * bare test-and-set + yield-retry loop with an owner_prio field that no
 * PI logic ever read (the header claimed priority inheritance; none
 * existed). Three cli windows make the object internally consistent on
 * this single CPU:
 *   (1) mutex_lock: test-and-set + owner registration are ONE window,
 *       so locked==1 always implies a registered, resolvable owner;
 *   (2) mutex_lock (contended): waiter registration + BLOCKED is ONE
 *       window (same lost-wakeup shape as sem_wait's P0-5 fix);
 *   (3) mutex_unlock: owner clear + release + FIFO sweep is ONE window.
 * mutex_unlock wakes the FIFO head, sweeping waiters whose tid died
 * while queued. The dead-owner case is also handled: a contended
 * mutex_lock that finds its owner destroyed/EXITED force-releases the
 * lock so the object cannot wedge forever (the dead holder's critical
 * section is not rolled back - documented exposure, like a POSIX mutex
 * held by a cancelled thread). There is deliberately NO priority
 * inheritance - see core_sync.h. */
void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = -1;
    m->num_waiters = 0;
    for (int i = 0; i < MUTEX_MAX_WAITERS; i++) m->waiters[i] = (tid_t)-1;
    m->lock_count = 0;
}

/* Pop the FIFO head that is still a live BLOCKED task. Caller must hold
 * the cli window; dead/no-longer-blocked tids are swept off the queue. */
static tid_t mutex_pop_live_waiter(mutex_t *m) {
    while (m->num_waiters > 0) {
        tid_t cand = m->waiters[0];
        for (int i = 1; i < m->num_waiters; i++)
            m->waiters[i - 1] = m->waiters[i];
        m->waiters[m->num_waiters - 1] = (tid_t)-1;
        m->num_waiters--;
        extern task_t *core_kthread_get_task(tid_t);  /* sched.h */
        task_t *t = core_kthread_get_task(cand);
        if (t && t->state == TASK_BLOCKED) return cand;
        /* Dead (EXITED/destroyed) or no longer blocked: drop it and
         * keep scanning (same policy as sem_post's P4 fix). */
    }
    return -1;
}

void mutex_lock(mutex_t *m) {
    tid_t my_tid = core_kthread_current_tid();
    for (;;) {
        u64 flags;
        __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
        if (__sync_lock_test_and_set(&m->locked, 1) == 0) {
            /* Window (1): the CAS and the owner registration are atomic
             * against every other mutex_lock/unlock on this single CPU,
             * so "locked && owner unregistered" can never be observed. */
            m->owner = my_tid;
            m->lock_count++;
            g_stats.mutex_locks++;
            if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
            return;
        }
        /* Contended. locked==1 now implies a valid owner (window (1)/(3)
         * invariants), so the dead-owner probe below is reliable. Note
         * owner may legitimately be tid 0 (the idle task hosts the shell
         * and its synctest/g7test threads), so the probe goes through
         * core_kthread_get_task for ANY owner value: -1 has no task
         * (-> force-release), 0 is the live idle task (-> normal
         * contention path). */
        task_t *ow = core_kthread_get_task(m->owner);
        if (ow == 0 || ow->state == TASK_EXITED) {
            /* A3-09: the holder was killed/destroyed while holding the
             * mutex. Force-release so the lock cannot stay locked
             * forever, sweep dead waiters and wake the first live one.
             * The dead holder's critical section is not rolled back
             * (documented exposure); the goal is that the lock object
             * itself recovers. */
            m->owner = -1;
            __sync_lock_release(&m->locked);
            tid_t to_wake = mutex_pop_live_waiter(m);
            if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
            if (to_wake >= 0) core_kthread_wake(to_wake);
            continue;  /* race the acquire again */
        }
        if (m->num_waiters < MUTEX_MAX_WAITERS) {
            /* Window (2): register as FIFO waiter AND go BLOCKED before
             * dropping the cli, so an unlock in between can always wake
             * us (it will find us registered and BLOCKED). */
            m->waiters[m->num_waiters++] = my_tid;
            task_t *me = core_kthread_current();
            if (me) me->state = TASK_BLOCKED;
            if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
            core_sched_yield();  /* BLOCKED; mutex_unlock will wake us */
            /* Woken: mutex_unlock/force-release popped us off the queue
             * before waking (the pop is what selects the wake target), so
             * the self-sweep below is an idempotent safety net, not the
             * primary removal path. Then loop back and re-contend through
             * the normal acquire path - unlock does not hand the lock
             * over, it wakes the FIFO head to re-contend. */
            __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
            for (int i = 0; i < m->num_waiters; i++) {
                if (m->waiters[i] == my_tid) {
                    for (int j = i; j < m->num_waiters - 1; j++)
                        m->waiters[j] = m->waiters[j + 1];
                    m->waiters[m->num_waiters - 1] = (tid_t)-1;
                    m->num_waiters--;
                    break;
                }
            }
            if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
        } else {
            /* Waiter table full: fail-open retry (same policy as
             * sem_wait's BUG-046 fix - never BLOCK without being
             * registered, or nobody can ever wake us). Stay RUNNING so
             * the scheduler just gives the CPU back later. */
            if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
            core_sched_yield();
        }
    }
}

void mutex_unlock(mutex_t *m) {
    /* P1-11 FIX: check that the caller actually owns the mutex. Done
     * inside window (3): on this single CPU the owner of a locked mutex
     * can only be the caller itself (it registered under cli in window
     * (1)), so a mismatch is a genuine user error and is ignored, as
     * before. */
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    tid_t my_tid = core_kthread_current_tid();
    if (m->owner != my_tid) {
        if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
        return;  /* not the owner: ignore */
    }
    m->owner = -1;
    g_stats.mutex_unlocks++;
    __sync_lock_release(&m->locked);
    tid_t to_wake = mutex_pop_live_waiter(m);
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
    if (to_wake >= 0) core_kthread_wake(to_wake);
    /* A3-09: the old unconditional core_sched_yield() on every unlock
     * cost an extra context switch even when nobody was waiting; waking
     * the FIFO head is sufficient. */
}

/* ---- Condition variable ---- */
void cond_init(cond_t *c) {
    spin_init(&c->lock);
    c->waiters = 0;
    c->signal_count = 0;
    for (int i = 0; i < COND_MAX_WAITERS; i++) c->waiter_tids[i] = (tid_t)-1;
}

void cond_wait(cond_t *c, mutex_t *m) {
    tid_t my_tid = core_kthread_current_tid();
    spin_lock(&c->lock);
    int registered = 0;
    for (;;) {
        c->waiters++;
        /* P0-5 FIX: register our TID AND set BLOCKED state while holding the
         * lock, so cond_signal can wake us without a lost-wakeup race. */
        for (int i = 0; i < COND_MAX_WAITERS; i++) {
            if (c->waiter_tids[i] == 0 || c->waiter_tids[i] == (tid_t)-1) {
                c->waiter_tids[i] = my_tid;
                registered = 1;
                break;
            }
        }
        if (registered) break;
        /* BUG-0105 FIX: the waiter table is FULL. The old code just fell
         * through: it marked us TASK_BLOCKED WITHOUT being registered,
         * so cond_signal could never find our tid — permanent sleep.
         * Retract the waiter count, drop c->lock AND the caller's mutex
         * (so a signaler can make progress), spin briefly and retry the
         * registration — the same fail-open pattern sem_wait uses since
         * its BUG-046 fix (with the BUG-0104 self-deadlock avoided by
         * letting the loop head re-take the lock). */
        c->waiters--;
        spin_unlock(&c->lock);
        mutex_unlock(m);
        for (volatile int spin = 0; spin < 1000; spin++);
        mutex_lock(m);
        spin_lock(&c->lock);
    }
    task_t *me = core_kthread_current();
    if (me) me->state = TASK_BLOCKED;
    spin_unlock(&c->lock);

    mutex_unlock(m);
    core_sched_yield();  /* give up CPU — we're BLOCKED, cond_signal will wake us */
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
    if (to_wake != (tid_t)-1) core_kthread_wake(to_wake);
    else core_sched_yield();
}

void cond_broadcast(cond_t *c) {
    spin_lock(&c->lock);
    if (c->waiters > 0) {
        c->signal_count += c->waiters;
        g_stats.cond_signals += c->waiters;
        /* P0-5 FIX: wake all waiters. core_kthread_wake only touches the
         * task table + ready queue under its own cli window (it never
         * sleeps or yields), so it is safe to call while holding this
         * irqsave spinlock. */
        for (int i = 0; i < COND_MAX_WAITERS; i++) {
            if (c->waiter_tids[i] != 0 && c->waiter_tids[i] != (tid_t)-1) {
                core_kthread_wake(c->waiter_tids[i]);
                c->waiter_tids[i] = (tid_t)-1;
            }
        }
    }
    spin_unlock(&c->lock);
    core_sched_yield();
}
