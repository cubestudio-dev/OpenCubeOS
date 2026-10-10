/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-AUDIT_P2-fix3 G7
 * File: kernel/core/g7test.c
 * Purpose: Scheduler/synchronization regression suite for the G7 batch
 *          (BUG-0282..0286). Runs from the kernel shell (the idle task,
 *          tid 0). Every assertion is double-sided where possible: the
 *          legal path must succeed AND the hostile/boundary input must
 *          be rejected. BUG-0281 (mmap) is covered by userprogs
 *          g7mmap.asm because it needs a real ring-3 address space. */
#include "g7test.h"
#include "types.h"
#include "core_sched.h"
#include "core_sync.h"
#include "mem_pmm.h"
#include "screen_console.h"
#include "lib_string.h"

/* OC_KTHREAD_STACK_PAGES is private to core_sched.c; the reaper frees
 * exactly this many frames per task stack. */
#define G7_KTHREAD_STACK_PAGES 6

static int g_fails  = 0;
static int g_checks = 0;

static void g7_pass(const char *what) {
    char buf[128];
    strcpy(buf, "  PASS ");
    strcat(buf, what);
    strcat(buf, "\n");
    screen_console_puts(buf);
}

static void g7_fail(const char *what, const char *detail) {
    char buf[192];
    strcpy(buf, "  FAIL ");
    strcat(buf, what);
    if (detail && detail[0]) { strcat(buf, " ("); strcat(buf, detail); strcat(buf, ")"); }
    strcat(buf, "\n");
    screen_console_puts(buf);
    g_fails++;
}

#define CHECK(cond, what, detail) \
    do { g_checks++; if (cond) g7_pass(what); else g7_fail(what, detail); } while (0)

/* ================================================================
 * A. spinlock irqsave/irqrestore semantics (BUG-0285 / A3-08)
 * ================================================================ */
static spinlock_t g7_spin;

static void g7_test_spin(void) {
    u64 f0, f1, f2;
    screen_console_puts("A: spinlock irqsave/irqrestore (BUG-0285)\n");
    spin_init(&g7_spin);

    __asm__ volatile("pushfq; popq %0" : "=r"(f0));
    CHECK((f0 & 0x200) != 0, "A1 pre: IF=1 in shell task context", "IF was 0");

    spin_lock(&g7_spin);
    __asm__ volatile("pushfq; popq %0" : "=r"(f1));
    CHECK((f1 & 0x200) == 0, "A2 spin_lock cleared IF (irqsave)", "IF still set inside lock");
    spin_unlock(&g7_spin);
    __asm__ volatile("pushfq; popq %0" : "=r"(f2));
    CHECK((f2 & 0x200) != 0, "A3 spin_unlock restored IF=1", "IF lost after unlock");

    /* Nested cli context: lock+unlock must leave IF exactly as found. */
    __asm__ volatile("cli");
    spin_lock(&g7_spin);
    spin_unlock(&g7_spin);
    __asm__ volatile("pushfq; popq %0" : "=r"(f2));
    CHECK((f2 & 0x200) == 0, "A4 lock/unlock inside cli keeps IF=0", "IF was re-enabled");
    __asm__ volatile("sti");

    /* trylock: same irqsave contract; fails on an already-held lock. */
    CHECK(spin_trylock(&g7_spin) == 1, "A5 trylock on free lock succeeds", "returned 0");
    __asm__ volatile("pushfq; popq %0" : "=r"(f1));
    CHECK((f1 & 0x200) == 0, "A6 trylock cleared IF", "IF set after trylock");
    CHECK(spin_trylock(&g7_spin) == 0, "A7 trylock on held lock fails", "returned 1");
    /* A8 must observe IF *before* the unlock: spin_unlock restores the IF
     * state recorded at ACQUIRE time (A5 ran with IF=1), so checking after
     * the unlock would read the restored IF=1 - correct irqrestore
     * behaviour, wrong place to assert. The failed trylock itself must
     * leave IF exactly as it found it (0, inherited from A6's window). */
    __asm__ volatile("pushfq; popq %0" : "=r"(f1));
    CHECK((f1 & 0x200) == 0, "A8 IF stays 0 after failed trylock", "IF re-enabled early");
    spin_unlock(&g7_spin);   /* restores IF=1 (A5 acquired with IF=1) */
    __asm__ volatile("sti"); /* symmetric no-op with the restore above */
}

/* ================================================================
 * B. mutex FIFO waiters + death handling (BUG-0286 / A3-09)
 * ================================================================ */
static mutex_t g7_m;
static mutex_t g7_m2;
static mutex_t g7_m3;
static volatile int g7_seq[8];
static volatile int g7_seq_n;
static volatile int g7_done2, g7_done3, g7_done5;
static volatile int g7_held6, g7_rel6;

static void g7_fn2(void *a) { (void)a;
    mutex_lock(&g7_m);
    g7_seq[g7_seq_n] = 2; g7_seq_n++;
    for (volatile int i = 0; i < 2000; i++) ;
    mutex_unlock(&g7_m);
    g7_done2 = 1;
}
static void g7_fn3(void *a) { (void)a;
    mutex_lock(&g7_m);
    g7_seq[g7_seq_n] = 3; g7_seq_n++;
    mutex_unlock(&g7_m);
    g7_done3 = 1;
}
static void g7_fn4(void *a) { (void)a;
    mutex_lock(&g7_m2);          /* will die while queued on m2 */
    g7_seq[g7_seq_n] = 4; g7_seq_n++;
    mutex_unlock(&g7_m2);
}
static void g7_fn5(void *a) { (void)a;
    mutex_lock(&g7_m2);
    g7_seq[g7_seq_n] = 5; g7_seq_n++;
    mutex_unlock(&g7_m2);
    g7_done5 = 1;
}
static void g7_fn6(void *a) { (void)a;
    mutex_lock(&g7_m3);          /* will die while HOLDING m3 */
    g7_held6 = 1;
    while (!g7_rel6) core_sched_yield();
    mutex_unlock(&g7_m3);
}

static void g7_wait_blocked(tid_t t) {
    for (int i = 0; i < 4000; i++) {
        task_t *tp = core_kthread_get_task(t);
        if (tp && tp->state == TASK_BLOCKED) return;
        core_sched_yield();
    }
}
static void g7_wait_flag(volatile int *f) {
    for (int i = 0; i < 40000 && !*f; i++) core_sched_yield();
}

static void g7_test_mutex(void) {
    screen_console_puts("B: mutex FIFO waiters + death handling (BUG-0286)\n");
    mutex_init(&g7_m);
    mutex_init(&g7_m2);
    mutex_init(&g7_m3);
    g7_seq_n = 0;
    g7_done2 = g7_done3 = g7_done5 = 0;
    g7_held6 = g7_rel6 = 0;

    /* B1-B5: strict FIFO wake order.
     * The test thread (tid 0) holds g7_m; T2 then T3 register as waiters
     * in that order; unlock must wake T2 first. */
    mutex_lock(&g7_m);
    tid_t t2 = core_kthread_create(g7_fn2, 0, "g7b2", 10);
    g7_wait_blocked(t2);
    tid_t t3 = core_kthread_create(g7_fn3, 0, "g7b3", 10);
    g7_wait_blocked(t3);
    CHECK(t2 > 0 && t3 > 0, "B1 spawned two contenders", "create failed");
    CHECK(core_kthread_get_task(t2) && core_kthread_get_task(t2)->state == TASK_BLOCKED,
          "B2 T2 registered as BLOCKED waiter", "T2 not blocked");
    CHECK(core_kthread_get_task(t3) && core_kthread_get_task(t3)->state == TASK_BLOCKED,
          "B3 T3 registered as BLOCKED waiter", "T3 not blocked");
    mutex_unlock(&g7_m);
    g7_wait_flag(&g7_done3);
    CHECK(g7_seq_n == 2, "B4 both contenders acquired the lock", "not both ran");
    CHECK(g7_seq_n == 2 && g7_seq[0] == 2 && g7_seq[1] == 3,
          "B5 FIFO wake order (T2 before T3)", "arrival order violated");

    /* B6-B8: a waiter that dies while queued is swept by unlock; the
     * next live contender still gets the lock. */
    mutex_lock(&g7_m2);
    tid_t t4 = core_kthread_create(g7_fn4, 0, "g7b4", 10);
    g7_wait_blocked(t4);
    core_kthread_destroy(t4);      /* T4 dies while queued on g7_m2 */
    mutex_unlock(&g7_m2);          /* must sweep the dead waiter */
#ifdef MUTEX_MAX_WAITERS
    /* Only the fixed core_sync.h has a waiter queue to inspect; this
     * keeps the same suite source compilable against the pre-fix
     * baseline for before/after evidence runs. */
    CHECK(g7_m2.num_waiters == 0, "B6 dead waiter swept from queue", "waiters remain");
#endif
    core_kthread_create(g7_fn5, 0, "g7b5", 10);   /* T5: the live contender */
    g7_wait_flag(&g7_done5);
    int saw4 = 0;
    for (int i = 0; i < g7_seq_n; i++) if (g7_seq[i] == 4) saw4 = 1;
    CHECK(saw4 == 0, "B7 dead waiter never acquired the lock", "T4 ran after destroy");
    CHECK(g7_done5 != 0, "B8 live contender T5 acquired lock after sweep", "T5 never got lock");

    /* B9: the HOLDER is killed while holding the mutex. The next
     * contended mutex_lock must force-release instead of deadlocking
     * forever. (On the pre-fix code this call hangs: locked stays 1
     * with an EXITED owner and the retry loop never terminates.) */
    tid_t t6 = core_kthread_create(g7_fn6, 0, "g7b6", 10);
    g7_wait_flag(&g7_held6);
    core_kthread_destroy(t6);      /* dies holding g7_m3 */
    mutex_lock(&g7_m3);            /* must recover via force-release */
    CHECK(g7_m3.owner == core_kthread_current_tid(),
          "B9 lock recovered after holder was killed", "owner not the caller");
    mutex_unlock(&g7_m3);
    g7_rel6 = 1;  /* harmless: T6 is gone; nothing reads it */
}

/* ================================================================
 * C. reaper mutual exclusion + accounting (BUG-0282 / A3-05)
 * ================================================================ */
static void g7_dummy_fn(void *a) { (void)a; for (;;) core_sched_yield(); }

static void g7_test_reap(void) {
    mem_pmm_stats_t s0, s1, s2;
    screen_console_puts("C: reaper mutual exclusion + accounting (BUG-0282)\n");

    mem_pmm_get_stats(&s0);
    tid_t t = core_kthread_create(g7_dummy_fn, 0, "g7reap", 20);
    mem_pmm_get_stats(&s1);
    CHECK(t > 0, "C1 spawned probe task with a real 6-page stack", "create failed");
    if (t <= 0) return;

    core_sched_task_exited(t);     /* EXITED + dequeued + destroy hooks */
    for (int i = 0; i < 4000 && core_kthread_get_task(t) != 0; i++)
        core_sched_yield();
    CHECK(core_kthread_get_task(t) == 0, "C2 EXITED task reaped (slot freed)", "slot still in use");
    mem_pmm_get_stats(&s2);
    CHECK(s2.free_pages >= s1.free_pages + G7_KTHREAD_STACK_PAGES,
          "C3 all 6 stack frames returned to PMM", "free count did not recover");

    /* Idempotence: further sweeps must not free anything again. A
     * double free would push free_pages up by G7_KTHREAD_STACK_PAGES
     * (or corrupt the bitmap - both change this count). */
    mem_pmm_get_stats(&s1);
    for (int i = 0; i < 20; i++) core_sched_yield();
    mem_pmm_get_stats(&s2);
    CHECK(s2.free_pages == s1.free_pages, "C4 reap is idempotent (no double free)", "free count drifted");
    (void)s0;
}

/* ================================================================
 * D. "idle task is never queued" invariant (BUG-0284 / A3-07)
 * ================================================================ */
static volatile int g7_idle_obs_state = -1;  /* state of tid 0 seen from the probe */
static volatile int g7_idle_done = 0;

static void g7_idle_probe_fn(void *arg) {
    (void)arg;
    task_t *it = core_kthread_get_task(0);
    for (int i = 0; i < 4000; i++) {
        core_sched_yield();        /* churn so ticks switch us out and back */
        if (it) g7_idle_obs_state = it->state;
    }
    g7_idle_done = 1;
}

static void g7_test_idle(void) {
    screen_console_puts("D: idle task is never queued (BUG-0284)\n");
    g7_idle_obs_state = -1;
    g7_idle_done = 0;
    tid_t t = core_kthread_create(g7_idle_probe_fn, 0, "g7idle", 1);
    CHECK(t > 0, "D1 spawned prio-1 probe", "create failed");
    if (t <= 0) return;
    g7_wait_flag(&g7_idle_done);
    /* While the probe runs, the idle task is switched away from. With
     * the fix it keeps state TASK_RUNNING and is NOT re-queued; the old
     * code pushed it into ready level 31, where it shows up as READY. */
    CHECK(g7_idle_obs_state == TASK_RUNNING,
          "D2 idle state stays RUNNING when switched away (never queued)",
          "idle observed READY (queued)");
}

/* ================================================================ */
int g7test_run(void) {
    g_fails = 0;
    g_checks = 0;
    screen_console_puts("G7 regression suite (BUG-0281..0286):\n");
    g7_test_spin();
    g7_test_mutex();
    g7_test_reap();
    g7_test_idle();
    {
        char buf[96]; char n[20];
        strcpy(buf, "G7TEST: ");
        u64_to_str((u64)(g_checks - g_fails), n); strcat(buf, n);
        strcat(buf, "/");
        u64_to_str((u64)g_checks, n); strcat(buf, n);
        strcat(buf, g_fails == 0 ? " PASS\n" : " FAIL\n");
        screen_console_puts(buf);
    }
    return g_fails;
}
