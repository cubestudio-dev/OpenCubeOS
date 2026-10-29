/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/diag ring-3 surface.
 *
 * New category "diag" (19): the home for the v2.2.0 diagnostic
 * commands the WP-03 nine-class services never had.  G8 fills the
 * first eight ops - the vitals family (mem/frag/heap/sched/syncstat,
 * verbatim bodies over the surviving PMM/heap/scheduler/sync stats
 * APIs) and the crash family (crashlog/crashtest/dumpinfo, moved from
 * recovery/shell_cmds_crash.c; the mechanism lives in
 * services/crash/core_crash.*).  Ops 9-39 are frozen in l1svc_defs.h
 * for the G9/G10 groups. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "mem_pmm.h"
#include "mem_heap.h"
#include "core_sched.h"
#include "core_sync.h"
#include "core_crash.h"
#include "screen_console.h"
#include "lib_string.h"

#include <stdint.h>

static char *s_out;
static int   s_left;

static void put(const char *s) {
    while (*s && s_left > 1) { *s_out++ = *s++; s_left--; }
}
static void put_u64(u64 v) {
    char b[24];
    u64_to_str(v, b);
    put(b);
}
static void put_hex0(u64 v) {
    char b[24];
    u64_to_hex(v, b, 0);
    put(b);
}

/* ---- vitals (v2.2.0 core/main.c bodies, verbatim) --------------------- */
static int diag_mem(void) {
    mem_pmm_stats_t s;
    mem_pmm_get_stats(&s);
    put("Physical memory:\n");
    put("  total: "); put_u64(s.total_bytes);
    put(" bytes ("); put_u64(s.total_pages); put(" pages)\n");
    put("  used:  "); put_u64(s.used_bytes);
    put(" bytes ("); put_u64(s.used_pages); put(" pages)\n");
    put("  free:  "); put_u64(s.free_bytes);
    put(" bytes ("); put_u64(s.free_pages); put(" pages)\n");
    put("  fragments: "); put_u64(s.free_fragments); put("\n");
    /* P4 fix: show cumulative alloc/free/fail counters. */
    put("  allocs: "); put_u64(s.total_allocs);
    put("  frees: "); put_u64(s.total_frees);
    put("  failures: "); put_u64(s.alloc_failures);
    put("\n");
    return 0;
}

static int diag_frag(void) {
    mem_pmm_stats_t ps;
    mem_pmm_get_stats(&ps);
    mem_heap_stats_t hs;
    mem_heap_get_stats(&hs);
    put("PMM: free_pages="); put_u64(ps.free_pages);
    put(" fragments="); put_u64(ps.free_fragments);
    if (ps.free_fragments > 0) {
        u64 avg = ps.free_pages / ps.free_fragments;
        put(" avg_frag="); put_u64(avg);
    }
    put("\n");
    put("Heap: free_blocks="); put_u64(hs.free_count);
    put(" free_bytes="); put_u64(hs.free);
    put("\n");
    return 0;
}

static int diag_heap(void) {
    mem_heap_stats_t s;
    mem_heap_get_stats(&s);
    put("Kernel heap:\n");
    put("  size:  "); put_u64(s.mem_heap_size); put(" bytes\n");
    put("  alloc: "); put_u64(s.allocated);
    put(" bytes ("); put_u64(s.alloc_count); put(" blocks)\n");
    put("  free:  "); put_u64(s.free);
    put(" bytes ("); put_u64(s.free_count); put(" blocks)\n");
    put("  overhead: "); put_u64(s.overhead); put(" bytes\n");
    put("  total allocs: "); put_u64(s.total_allocs);
    put("  total frees: "); put_u64(s.total_frees);
    put("\n");
    return 0;
}

static int diag_sched(void) {
    core_sched_stats_t s;
    core_sched_get_stats(&s);
    put("switches="); put_u64(s.total_switches);
    put(" preemptions="); put_u64(s.total_preemptions);
    put(" current_tid="); put_u64(s.current_tid);
    put(" active_tasks="); put_u64((u64)s.active_tasks);
    put("\n");
    return 0;
}

static int diag_syncstat(void) {
    sync_stats_t s;
    sync_get_stats(&s);
    put("spin_locks="); put_u64(s.spin_locks);
    put(" spin_unlocks="); put_u64(s.spin_unlocks);
    put("\n");
    put("sem_waits="); put_u64(s.sem_waits);
    put(" sem_posts="); put_u64(s.sem_posts);
    put("\n");
    put("mutex_locks="); put_u64(s.mutex_locks);
    put(" mutex_unlocks="); put_u64(s.mutex_unlocks);
    put("\n");
    put("cond_waits="); put_u64(s.cond_waits);
    put(" cond_signals="); put_u64(s.cond_signals);
    put("\n");
    return 0;
}

/* ---- crash family (v2.2.0 recovery/shell_cmds_crash.c bodies) --------- */
static int diag_crashlog(const char *args) {
    while (args && *args == ' ') args++;
    if (args && strcmp(args, "flush") == 0) {
        int n = crashlog_flush();
        if (n < 0) {
            put("crashlog: flush FAILED (no filesystem mounted?)\n");
            return 1;
        }
        put("crashlog: flushed ");
        put_u64((u64)n);
        put(" bytes to /var/log/crash.log\n");
        return 0;
    }
    int count = crashlog_count();
    if (count == 0) {
        put("crashlog: no exceptions recorded\n");
        return 0;
    }
    put("crashlog: "); put_u64((u64)count); put(" entries\n");
    for (int i = 0; i < count; i++) {
        const crashlog_entry_t *e = crashlog_get(i);
        if (!e) break;
        put("  ["); put_u64((u64)i); put("] ");
        put(crash_src_name(e->vector));
        put(" ring="); put_u64(e->ring);
        put(" err=0x"); put_hex0(e->error_code);
        put(" rip=0x"); put_hex0(e->rip);
        put(" rsp=0x"); put_hex0(e->rsp);
        if (e->cr2) { put(" cr2=0x"); put_hex0(e->cr2); }
        if (e->rtc_epoch) {
            put(" @"); put_u64(e->rtc_epoch); put("s");
        } else {
            put(" @tick="); put_u64(e->jiffies);
        }
        if (e->task[0]) { put(" task="); put(e->task); }
        if (e->pid > 0) { put(" pid="); put_u64((u64)e->pid); }
        put("\n");
    }
    return 0;
}

/* WP-11 4.4: crashtest ovf - burn a dedicated kthread's stack until the
 * guard page panics.  Runs in its own task so the caller's stack is
 * never the one burning. */
static int crashtest_ovf_depth = 0;
static void (*crashtest_ovf_next)(void) = 0;
static void crashtest_ovf_recurse(void) {
    volatile char pad[4096 - 64];
    (void)pad;
    crashtest_ovf_depth++;
    crashtest_ovf_next();   /* indirect: defeats -Winfinite-recursion */
    pad[0] = 1;             /* live after the call: blocks tail-call
                             * optimization, so each level REALLY pushes
                             * a fresh ~4 KiB frame (a tail-called loop
                             * would reuse the frame and never grow). */
}
static void crashtest_ovf_task(void *arg) {
    (void)arg;
    screen_console_puts("crashtest-ovf: task running, burning the stack...\n");
    crashtest_ovf_next = crashtest_ovf_recurse;
    crashtest_ovf_recurse();
    screen_console_puts("crashtest-ovf: returned WITHOUT faulting (BAD)\n");
}

static int diag_crashtest(const char *args) {
    while (args && *args == ' ') args++;
    if (!args || !args[0] || strcmp(args, "help") == 0) {
        put(
            "usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]\n"
            "  panic   : kernel_panic() direct\n"
            "  exc     : ring-0 #UD exception\n"
            "  pf      : ring-0 unmapped-address write fault\n"
            "  assert  : OC_ASSERT(0)\n"
            "  wd      : forced watchdog timeout\n"
            "  ovf     : kernel stack overflow (guard page)\n"
            "  dump    : capture a core dump without panicking\n"
            "  policy  : show/set the after-panic policy\n");
        return 0;
    }
    if (strcmp(args, "policy") == 0) {
        put("crashtest: policy = ");
        put(crash_recovery_policy_name(crash_recovery_get_policy()));
        put("\n");
        return 0;
    }
    if (strncmp(args, "policy ", 7) == 0) {
        const char *v = args + 7;
        while (*v == ' ') v++;
        int p = -1;
        if (strcmp(v, "halt") == 0 || strcmp(v, "0") == 0) p = CRASH_POLICY_HALT;
        if (strcmp(v, "reboot") == 0 || strcmp(v, "1") == 0) p = CRASH_POLICY_REBOOT;
        if (p < 0) {
            put("crashtest: policy must be halt or reboot\n");
            return 1;
        }
        crash_recovery_set_policy(p);
        put("crashtest: policy set - after the next panic the kernel will ");
        put(p == CRASH_POLICY_REBOOT ? "REBOOT\n" : "HALT\n");
        return 0;
    }
    if (strcmp(args, "panic") == 0) {
        put("crashtest: triggering kernel_panic()...\n");
        kernel_panic("manual crashtest: kernel_panic()");
        return 0; /* not reached when policy=halt */
    }
    if (strcmp(args, "exc") == 0) {
        put("crashtest: triggering ring-0 #UD...\n");
        __asm__ volatile(".byte 0x0F, 0x0B\n");
        put("crashtest: #UD returned unexpectedly\n");
        return 1;
    }
    if (strcmp(args, "pf") == 0) {
        put("crashtest: triggering ring-0 unmapped write...\n");
        /* Write to a canonical-but-unmapped high address (PML4[0..3]
         * only cover the low 2 TiB identity window; 0x700000000000 is
         * PML4[224] = guaranteed not-present). Inline asm because a C
         * constant deref of NULL compiles to ud2 (UB) AND virtual page
         * 0 is identity-mapped anyway (real-mode IVT page) - neither
         * would raise the #PF this variant exists to test. */
        {
            u64 val = 0x0BADBEEF0BADBEEFULL;
            u64 addr = 0x0000700000000000ULL;
            __asm__ volatile("movq %0, (%1)"
                             :: "r"(val), "r"(addr) : "memory");
        }
        put("crashtest: unmapped write returned unexpectedly\n");
        return 1;
    }
    if (strcmp(args, "assert") == 0) {
        put("crashtest: triggering OC_ASSERT(0)...\n");
        OC_ASSERT(0);
        put("crashtest: assert returned unexpectedly\n");
        return 1;
    }
    if (strcmp(args, "wd") == 0) {
        put("crashtest: forcing watchdog timeout (armed + rewound heartbeat)...\n");
        extern void crash_wdt_force_timeout(void);
        crash_wdt_force_timeout();
        put("crashtest: waiting for the next watchdog tick...\n");
        for (;;) { __asm__ volatile("sti"); __asm__ volatile("hlt"); }
        return 0;
    }
    if (strcmp(args, "ovf") == 0) {
        put("crashtest: spawning kstack-ovf task (4 KiB frames until the guard page)...\n");
        tid_t tid = core_kthread_create(crashtest_ovf_task, NULL,
                                        "kstack-ovf", 20);
        if (tid < 0) {
            put("crashtest: task creation FAILED\n");
            return 1;
        }
        put("crashtest: kstack-ovf task started - expect a guard-page panic\n");
        return 0;
    }
    if (strcmp(args, "dump") == 0) {
        int n = core_dump_save("manual crashtest dump", NULL);
        if (n < 0) {
            put("crashtest: dump FAILED (no boot-time buffer)\n");
            return 1;
        }
        put("crashtest: captured ");
        put_u64((u64)n);
        put(" bytes, ");
        put(core_dump_persisted() ? "persisted to /var/crash/core.dump"
                                  : "serial-only (no filesystem)");
        put(" - see dumpinfo\n");
        return 0;
    }
    put("crashtest: unknown variant '");
    put(args);
    put("' (try: crashtest help)\n");
    return 1;
}

static int diag_dumpinfo(void) {
    put("core dump status:\n");

    u32 size = core_dump_size();
    if (size == 0) {
        put("  no dump captured yet (trigger: crashtest panic|exc|pf)\n");
    } else {
        /* read the header back through the public API and decode it
         * with the REAL layout (oc_dump_hdr_t from core_crash.h) */
        static u8 hdr_img[256];
        u32 total = 0;
        int got = core_dump_read(hdr_img, sizeof(hdr_img), &total);
        const oc_dump_hdr_t *h = (const oc_dump_hdr_t *)hdr_img;
        int magic_ok = (got >= 8 && h->magic[0]=='O' && h->magic[1]=='C' &&
                        h->magic[2]=='D' && h->magic[3]=='U' &&
                        h->magic[4]=='M' && h->magic[5]=='P');
        put("  image: "); put_u64(total);
        put(" bytes, magic ");
        put(magic_ok ? "OCDUMP01 OK" : "BAD");
        put(", ");
        put(core_dump_persisted()
                        ? "persisted to /var/crash/core.dump"
                        : "serial-only (no filesystem)");
        put("\n");

        if (magic_ok) {
            static char vbuf[64];   /* sized for reason[40] - the v2.2.0
                                    * body read it into a 24-byte stack
                                    * buffer and got away with the
                                    * overlap only by layout luck */
            put("  build: "); put_u64(h->build);
            put("  version: ");
            strncpy(vbuf, h->version, 23); vbuf[23] = 0; put(vbuf);
            put("\n");
            put("  reason: ");
            strncpy(vbuf, h->reason, 39); vbuf[39] = 0; put(vbuf);
            put("\n");
            put("  task: ");
            strncpy(vbuf, h->task, 15); vbuf[15] = 0; put(vbuf);
            put(" tid="); put_u64(h->tid);
            put(" regs=");
            put(h->regs_present ? "exception-frame" : "live-snapshot");
            put(" stack=");
            put_u64(h->stack_copy_len);
            put("/");
            put_u64(h->stack_size);
            put("B\n");
        }
    }

    put("policy: ");
    put(crash_recovery_policy_name(crash_recovery_get_policy()));
    put("  watchdog: ");
    put(crash_wdt_armed() ? "armed" : "off");
    put(" (timeout 15000 ms)\n");
    return 0;
}

static int diag_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
    case L1SVC_DIAG_MEM:       return diag_mem();
    case L1SVC_DIAG_FRAG:      return diag_frag();
    case L1SVC_DIAG_HEAP:      return diag_heap();
    case L1SVC_DIAG_SCHED:     return diag_sched();
    case L1SVC_DIAG_SYNCSTAT:  return diag_syncstat();
    case L1SVC_DIAG_CRASHLOG:  return diag_crashlog(a);
    case L1SVC_DIAG_CRASHTEST: return diag_crashtest(a);
    case L1SVC_DIAG_DUMPINFO:  return diag_dumpinfo();
    default: return -1;   /* ops 9-39: the G9/G10 groups land next */
    }
}

OC_SERVICE_DECL_L1("diag", "diag service (vitals + crash family + ring-0 self-tests + chipset diagnostics + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   diag_l1_dispatch);
