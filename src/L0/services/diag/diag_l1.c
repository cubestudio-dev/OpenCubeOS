/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/diag ring-3 surface.
 *
 * New category "diag" (19): the home for the v2.2.0 diagnostic
 * commands the WP-03 nine-class services never had.  G8 filled the
 * first eight ops - the vitals family (mem/frag/heap/sched/syncstat,
 * verbatim bodies over the surviving PMM/heap/scheduler/sync stats
 * APIs) and the crash family (crashlog/crashtest/dumpinfo, moved from
 * recovery/shell_cmds_crash.c; the mechanism lives in
 * services/crash/core_crash.*).  G9 fills ops 9-26 (the ring-0
 * self-test family, verbatim bodies moved from v2.2.0 core/main.c;
 * the exception self-test block lives in diag_exc_test.c so kmain's
 * boot step 11 and the `exc` op share one counter, exactly the
 * v2.2.0 sharing).  Five ops whose v2.2.0 semantics depended on
 * running with the KERNEL address space current (vmmap/pftest/
 * cr3test/vmkernelpt walk or map through current CR3) run inside a
 * switch-to-kernel-AS wrap - recreating the v2.2.0 execution context
 * from the ring-3 syscall (spawner AS restored before return).
 * Ops 27-39 stay frozen in l1svc_defs.h for the G10 chipset group. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "mem_pmm.h"
#include "mem_heap.h"
#include "mem_vmm.h"
#include "core_sched.h"
#include "core_sync.h"
#include "core_timer.h"
#include "core_crash.h"
#include "screen_console.h"
#include "lib_string.h"
#include "arch_exceptions.h"
#include "arch_idt.h"
#include "arch_irq.h"
#include "arch_lapic.h"
#include "driver_pci.h"
#include "driver_pcie.h"
#include "fs_vfs.h"
#include "fs_ramfs.h"
#include "diag_kern.h"

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
/* width-controlled hex (v2.2.0 bodies used u64_to_hex(v, buf, N) with
 * N = 1/2/8/12 paddings; keep the exact paddings for byte parity) */
static void put_hex(u64 v, int digits) {
    char b[24];
    u64_to_hex(v, b, digits);
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

/* ---- G9: ring-0 self-test family (v2.2.0 core/main.c bodies) ----------
 * Ports run inside the caller's SYS_L1SVC, i.e. kernel context with the
 * caller's address space current and interrupts re-enabled (see
 * sys_l1svc).  kernel-as() wraps the ops whose v2.2.0 semantics depend
 * on current CR3 being the kernel address space; the wrap restores the
 * spawner's AS before the op returns. */
static mem_vmm_as_t diag_kernel_as_enter(void) {
    mem_vmm_as_t saved = mem_vmm_current_as();
    mem_vmm_switch_as(mem_vmm_kernel_as());
    return saved;
}
static void diag_kernel_as_leave(mem_vmm_as_t saved) {
    mem_vmm_switch_as(saved);
}

/* ---- exc (op 10) - v2.2.0 core/main.c shell_cmd_exc, verbatim ------- */
static int diag_exc(void) {
    put("Running exception self-test (#DE, #UD, #PF)...\n");
    u64 before = g_exc_test_pass_count;
    arch_exc_tests();
    u64 after = g_exc_test_pass_count;
    put("passed ");
    put_u64(after - before);
    put(" exceptions, kernel alive\n");
    return 0;
}

/* ---- timer (op 20) - v2.2.0 core/main.c shell_cmd_timer ------------- */
/* v2.2.0's oneshot_test_cb bumped g_oneshot_fires, printed by `stats`
 * as "one="; the 3.0.0 stats op (services/display) prints the same
 * field from g_disp_oneshot_fires - the callback bumps THAT counter so
 * the observable "timer; ...; stats" chain keeps v2.2.0 parity. */
extern u64 g_disp_oneshot_fires;    /* services/display/disp_cmds_kern.c */
static void diag_oneshot_test_cb(void *ctx) {
    (void)ctx;
    g_disp_oneshot_fires++;
}
static int diag_timer(void) {
    int id = core_timer_register_oneshot(diag_oneshot_test_cb, NULL, 500);
    if (id >= 0) put("Registered 500ms one-shot timer\n");
    else         put("Failed to register timer\n");
    return 0;
}

/* ---- vmmap (op 15) - v2.2.0 shell_cmd_vmmap, kernel-AS wrapped ------ */
static int diag_vmmap(void) {
    mem_vmm_as_t saved = diag_kernel_as_enter();
    mem_vmm_as_t as = mem_vmm_current_as();
    u64 *pml4 = (u64*)as;
    put("Kernel address space (PML4 at 0x");
    put_hex(as, 12);
    put("):\n");

    for (int i = 0; i < 512; i++) {
        if (!(pml4[i] & VMM_FLAG_PRESENT)) continue;
        u64 vstart = (u64)i << 39;
        if (i >= 256) vstart |= 0xFF00000000000000ULL;  /* sign-extend */
        put("  PML4["); put_u64((u64)i);
        put("] vaddr=0x"); put_hex(vstart, 12);
        put(" -> 0x"); put_hex(pml4[i] & 0x000FFFFFFFFFF000ULL, 12);
        if (pml4[i] & 0x80) put(" (huge)");
        put("\n");
    }

    mem_vmm_fault_stats_t fs;
    mem_vmm_get_fault_stats(&fs);
    put("\nPage faults: total="); put_u64(fs.total_faults);
    put(" legal="); put_u64(fs.legal_faults);
    put(" illegal="); put_u64(fs.illegal_faults);
    put(" stack="); put_u64(fs.stack_growth);
    put(" heap="); put_u64(fs.mem_heap_growth);
    put("\n");
    diag_kernel_as_leave(saved);
    return 0;
}

/* ---- vmtest (op 13) - v2.2.0 shell_cmd_vmtest, verbatim ------------- */
static int diag_vmtest(void) {
    put("VMM test:\n");

    mem_vmm_as_t as = mem_vmm_create_address_space();
    if (as == 0) {
        put("  FAIL: could not create address space\n");
        return 1;
    }
    put("  created AS at 0x"); put_hex(as, 12); put("\n");

    u64 test_vaddr = 0x100000000ULL;
    u64 phys = mem_pmm_alloc_frame();
    if (phys == 0) {
        put("  FAIL: out of physical memory\n");
        mem_vmm_destroy_address_space(as);
        return 1;
    }
    int rc = mem_vmm_map_page(as, test_vaddr, phys, VMM_FLAGS_USER);
    put("  map_page(0x"); put_hex(test_vaddr, 12);
    put(" -> 0x"); put_hex(phys, 12);
    put(") = "); put_u64((u64)(i64)rc); put("\n");

    u64 mapped_phys;
    int mapped = mem_vmm_is_mapped(as, test_vaddr, &mapped_phys);
    put("  is_mapped = "); put_u64((u64)mapped);
    if (mapped) {
        put(" phys=0x"); put_hex(mapped_phys, 12);
    }
    put("\n");

    rc = mem_vmm_protect_page(as, test_vaddr, VMM_FLAG_PRESENT | VMM_FLAG_USER);
    put("  protect(read-only) = "); put_u64((u64)(i64)rc); put("\n");

    u64 old = mem_vmm_unmap_page(as, test_vaddr);
    put("  unmap returned 0x"); put_hex(old, 12); put("\n");

    mapped = mem_vmm_is_mapped(as, test_vaddr, NULL);
    put("  is_mapped after unmap = "); put_u64((u64)mapped); put("\n");

    mem_pmm_free_frame(phys);
    mem_vmm_destroy_address_space(as);
    put("  destroyed AS\n");
    if (mapped == 0) put("  PASS\n");
    else             put("  PARTIAL (unmap check failed)\n");
    return 0;
}

/* ---- memtest (op 16) - v2.2.0 shell_cmd_memtest, verbatim ------------ */
static int diag_memtest(void) {
    put("PMM test:\n");

    u64 pages[10];
    int ok = 1;
    for (int i = 0; i < 10; i++) {
        pages[i] = mem_pmm_alloc_frame();
        if (pages[i] == 0) { ok = 0; break; }
    }
    put("  alloc 10 pages: "); put(ok ? "OK" : "FAIL"); put("\n");

    for (int i = 0; i < 10; i++) {
        if (pages[i]) mem_pmm_free_frame(pages[i]);
    }
    put("  free 10 pages: OK\n");

    put("Heap test:\n");
    void *ptrs[5];
    ok = 1;
    for (int i = 0; i < 5; i++) {
        ptrs[i] = kmalloc(64 * (i + 1));
        if (!ptrs[i]) { ok = 0; break; }
        memset(ptrs[i], 0xAB, 64 * (i + 1));
    }
    put("  kmalloc 5 blocks: "); put(ok ? "OK" : "FAIL"); put("\n");

    if (ptrs[0]) {
        void *np = krealloc(ptrs[0], 512);
        if (np) {
            ptrs[0] = np;
            put("  krealloc to 512: OK\n");
        } else {
            put("  krealloc to 512: FAIL\n");
        }
    }

    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }
    put("  kfree all: OK\n");

    void *d = kmalloc(32);
    kfree(d);
    kfree(d);  /* should not crash */
    put("  double-free detection: OK (no crash)\n");

    put("  PASS\n");
    return 0;
}

/* ---- pftest (op 11) - v2.2.0 shell_cmd_pftest, kernel-AS wrapped ----- */
static volatile u8 g_pftest_illegal_armed = 0;  /* 0=disarmed, 1=test2, 2=test1 */
static volatile u8 g_pftest_illegal_caught = 0;
#define P2_14_TEST_VADDR 0xFFFF810000000000ULL
#define P4_TEST1_VADDR_CONST 0x100000000ULL  /* 4 GiB - above identity map */
u8 g_pftest_stack_ok = 0;    /* v2.2.0 core/main.c global, set on test-1 pass */

static int pftest_illegal_handler(u64 vaddr, u64 error_code, u64 rip) {
    (void)error_code;
    (void)rip;
    if (g_pftest_illegal_armed) {
        u64 page_vaddr = vaddr & ~0xFFF;
        int matches = 0;
        if (g_pftest_illegal_armed == 1 && page_vaddr == P2_14_TEST_VADDR) matches = 1;
        if (g_pftest_illegal_armed == 2 && page_vaddr == P4_TEST1_VADDR_CONST) matches = 1;
        if (matches) {
            u64 page = mem_pmm_alloc_frame();
            if (page == 0) return 0;  /* let L0 abort */
            if (mem_vmm_map_page(mem_vmm_current_as(), page_vaddr, page,
                             VMM_FLAG_PRESENT | VMM_FLAG_WRITE) != 0) {
                mem_pmm_free_frame(page);
                return 0;
            }
            g_pftest_illegal_caught = 1;
            return 1;  /* handled - resume the faulting instruction */
        }
    }
    return 0;  /* not our test - fall through to L0 default */
}

static int diag_pftest(void) {
    mem_vmm_as_t saved = diag_kernel_as_enter();
    put("Page fault test:\n");

    /* Test 1: counter test on a high unmapped address (see the v2.2.0
     * comment: kernel-context stack growth cannot be tested from the
     * kernel itself; this exercises the fault dispatcher + L1 handler). */
    put("  test 1: illegal-PF-on-high-address (counter test)...\n");
    mem_vmm_fault_stats_t fs1_before, fs1_after;
    mem_vmm_get_fault_stats(&fs1_before);
    mem_vmm_register_fault_handler(pftest_illegal_handler);
    g_pftest_illegal_caught = 0;
    g_pftest_illegal_armed = 2;  /* 2 = test 1 mode */
    {
        volatile u8 *p1 = (volatile u8*)(uintptr_t)P4_TEST1_VADDR_CONST;
        __asm__ volatile("" ::: "memory");
        *p1 = 0x42;
        __asm__ volatile("" ::: "memory");
        u8 rb = *p1;
        (void)rb;
    }
    g_pftest_illegal_armed = 0;
    mem_vmm_get_fault_stats(&fs1_after);
    {
        u64 delta = fs1_after.total_faults - fs1_before.total_faults;
        u64 legal_d = fs1_after.legal_faults - fs1_before.legal_faults;
        put("    faults delta="); put_u64(delta);
        put(" legal_delta="); put_u64(legal_d);
        put("\n");
        if (delta > 0 && legal_d > 0) {
            put("    OK (L1 handler caught the high-address PF)\n");
            g_pftest_stack_ok = 1;
        } else {
            put("    FAIL (no PF counted)\n");
        }
    }

    /* Test 2: illegal PF on an unmapped high-half address; the L1
     * handler maps a writable page so the faulting store resumes. */
    put("  test 2: illegal PF (unmapped address)...\n");
    mem_vmm_fault_stats_t fs_before, fs_after;
    mem_vmm_get_fault_stats(&fs_before);

    mem_vmm_register_fault_handler(pftest_illegal_handler);
    g_pftest_illegal_caught = 0;
    g_pftest_illegal_armed = 1;

    volatile u8 *bad = (volatile u8 *)(uintptr_t)P2_14_TEST_VADDR;
    __asm__ volatile("" ::: "memory");  /* full memory barrier */
    *bad = 0x57;
    __asm__ volatile("" ::: "memory");
    u8 readback = *bad;
    __asm__ volatile("" ::: "memory");

    g_pftest_illegal_armed = 0;

    mem_vmm_get_fault_stats(&fs_after);
    put("    faults before="); put_u64(fs_before.total_faults);
    put(" after="); put_u64(fs_after.total_faults);
    put("\n");
    put("    legal="); put_u64(fs_after.legal_faults);
    put(" illegal="); put_u64(fs_after.illegal_faults);
    put(" stack="); put_u64(fs_after.stack_growth);
    put(" heap="); put_u64(fs_after.mem_heap_growth);
    put("\n");

    int legal_delta = (int)(fs_after.legal_faults - fs_before.legal_faults);
    if (g_pftest_illegal_caught && readback == 0x57 && legal_delta > 0) {
        put("    L1 handler ran, page mapped, write verified, ");
        put_u64((u64)legal_delta);
        put(" legal PFs counted\n");
        put("  PASS\n");
    } else {
        put("    FAIL: L1 handler did not catch the illegal PF\n");
        put("    caught="); put_u64(g_pftest_illegal_caught);
        put(" readback=0x"); put_hex((u64)readback, 2);
        put(" legal_delta="); put_u64((u64)(i64)legal_delta);
        put("\n");
    }
    diag_kernel_as_leave(saved);
    return 0;
}

/* ---- cr3test (op 9) - v2.2.0 shell_cmd_cr3test, kernel-AS wrapped ----- */
static int diag_cr3test(void) {
    mem_vmm_as_t saved = diag_kernel_as_enter();
    put("CR3 switch test:\n");

    mem_vmm_as_t orig_as = mem_vmm_current_as();
    put("  current CR3=0x"); put_hex(orig_as, 12); put("\n");

    mem_vmm_as_t new_as = mem_vmm_create_address_space();
    if (new_as == 0) { put("  FAIL: create_address_space\n"); diag_kernel_as_leave(saved); return 1; }
    put("  new AS CR3=0x"); put_hex(new_as, 12); put("\n");

    u64 phys = mem_pmm_alloc_frame();
    if (phys == 0) {
        put("  FAIL: alloc_frame (PMM exhausted)\n");
        mem_vmm_destroy_address_space(new_as);
        diag_kernel_as_leave(saved);
        return 1;
    }
    mem_vmm_map_page(new_as, 0x100000000ULL, phys, VMM_FLAGS_USER);
    u64 mapped;
    int ok = mem_vmm_is_mapped(new_as, 0x100000000ULL, &mapped);
    put("  map at 4GiB: "); put(ok ? "OK" : "FAIL"); put("\n");

    mem_vmm_switch_as(new_as);
    put("  switched CR3 to 0x"); put_hex(mem_vmm_current_as(), 12); put("\n");

    ok = mem_vmm_is_mapped(new_as, 0x100000000ULL, &mapped);
    put("  verify mapped in new AS: "); put(ok ? "OK" : "FAIL");
    if (ok) {
        put(" phys=0x"); put_hex(mapped, 12);
    }
    put("\n");

    mem_vmm_switch_as(orig_as);
    put("  switched back to 0x"); put_hex(mem_vmm_current_as(), 12); put("\n");

    mem_vmm_unmap_page(new_as, 0x100000000ULL);
    mem_pmm_free_frame(phys);
    mem_vmm_destroy_address_space(new_as);
    put("  PASS\n");
    diag_kernel_as_leave(saved);
    return 0;
}

/* ---- irqabitest (op 19) - v2.2.0 shell_cmd_irqabitest, verbatim ------- */
static int diag_irqabitest(void) {
    extern void (*g_arch_irq_entry_probe)(void);
    extern u64 g_arch_irq_probe_result;
    extern void arch_abi_probe_entry(void);
    u64 start, now;

    put("IRQ-entry ABI test (BUG-0137 align / BUG-0138 DF):\n");

    g_arch_irq_probe_result = 0;
    g_arch_irq_entry_probe = arch_abi_probe_entry;

    /* 1) alignment + DF via real timer interrupts (ring0 source). */
    start = core_timer_now_ms();
    do { now = core_timer_now_ms(); } while (now - start < 120);
    u64 bits_irq = g_arch_irq_probe_result;
    g_arch_irq_probe_result = 0;   /* phase 2 accumulates from zero */

    /* 2) DF=1 injection through a ring0 software interrupt. */
    __asm__ volatile (
        "movq $0xFFFFFFFF, %%rax\n\t"
        "std\n\t"
        "int $0x80\n\t"
        "cld\n\t"
        ::: "rax", "rcx", "r11", "memory");
    u64 bits_int = g_arch_irq_probe_result;

    g_arch_irq_entry_probe = 0;

    put("  timer-IRQ bits=0x"); put_u64(bits_irq);
    put("  int0x80 bits=0x"); put_u64(bits_int);
    put("\n");
    if ((bits_irq & 3) == 3 && (bits_int & 3) == 3) {
        put("  PASS\n");
        return 0;
    }
    put("  FAIL\n");
    return 1;
}

/* ---- vmkernelpt (op 14) - v2.2.0 shell_cmd_vmkernelpt, kernel-AS wrap */
static int diag_vmkernelpt(void) {
    mem_vmm_as_t saved = diag_kernel_as_enter();
    int fail = 0;
    put("VMM kernel-PT test (BUG-0136 PT_OWNER):\n");

    mem_vmm_as_t kas = mem_vmm_current_as();   /* == kernel AS inside the wrap */

    volatile u8 *probe = (volatile u8*)0x200000;
    u8 before = *probe;                       /* read via the 2 MiB page */

    int rc = mem_vmm_map_page(kas, 0x200000, 0x200000, VMM_FLAGS_KERNEL);
    if (rc != 0) { put("  FAIL: kernel split map failed\n"); diag_kernel_as_leave(saved); return 1; }
    u8 after = *probe;
    if (after != before) {
        put("  FAIL: kernel data changed after split\n");
        fail = 1;
    }

    u64 kcr3 = (u64)mem_vmm_kernel_as();
    u64 *kpdpt = (u64*)(*(u64*)kcr3 & 0x000FFFFFFFFFF000ULL);
    u64 *kpd0  = (u64*)(kpdpt[0] & 0x000FFFFFFFFFF000ULL);
    u64 pt_phys = kpd0[1] & 0x000FFFFFFFFFF000ULL;

    {
        extern mem_vmm_as_t create_user_address_space(void);
        mem_vmm_as_t uas = create_user_address_space();
        if (uas == 0) { put("  FAIL: create_user_address_space failed\n"); diag_kernel_as_leave(saved); return 1; }
        put("  user AS created (copies split PD0)\n");
        mem_vmm_destroy_address_space(uas);
        put("  user AS destroyed\n");
    }

    u8 after_destroy = *probe;
    if (after_destroy != before) {
        put("  FAIL: kernel 0x200000 unreadable/corrupted after user AS destroy\n");
        fail = 1;
    }
    {
        extern int mem_pmm_frame_is_free(u64 paddr);
        if (mem_pmm_frame_is_free(pt_phys)) {
            put("  FAIL: kernel PT frame was FREED by user-AS destroy (BUG-0136)\n");
            fail = 1;
        }
    }

    mem_pmm_stats_t ps;
    mem_pmm_get_stats(&ps);
    put("  pmm failures="); put_u64(ps.alloc_failures);
    put("\n");
    if (ps.alloc_failures > 0) { put("  FAIL: PMM saw allocation failures\n"); fail = 1; }

    diag_kernel_as_leave(saved);
    if (!fail) { put("  PASS\n"); return 0; }
    put("  FAIL\n");
    return 1;
}

/* ---- heapbounds (op 18) - v2.2.0 shell_cmd_heapbounds, verbatim ------- */
static int diag_heapbounds(void) {
    int fail = 0;
    put("Heap bounds test (BUG-0140 wrap / BUG-0141 ceiling):\n");

    void *p1 = kmalloc(0xFFFFFFFFFFFFFFF1ULL);   /* rounds to 0 pre-fix */
    if (p1) { put("  FAIL: wrap size 0xFFFFFFFFFFFFFFF1 was served\n"); kfree(p1); fail = 1; }
    void *p2 = kmalloc(0xFFFFFFFFFFFFFFFFULL);   /* wraps to 0 pre-fix */
    if (p2) { put("  FAIL: wrap size 0xFFFFFFFFFFFFFFFF was served\n"); kfree(p2); fail = 1; }
    void *p3 = kmalloc(64ULL * 1024 * 1024 * 1024); /* 64 GiB, over ceiling */
    if (p3) { put("  FAIL: 64 GiB size was served\n"); kfree(p3); fail = 1; }

    void *p4 = kmalloc(4096);
    if (!p4) { put("  FAIL: legitimate 4096 B alloc refused\n"); fail = 1; }
    else {
        memset(p4, 0x5A, 4096);
        u8 *b4 = (u8*)p4;
        if (b4[0] != 0x5A || b4[4095] != 0x5A) {
            put("  FAIL: 4096 B block not fully writable\n"); fail = 1;
        }
        kfree(p4);
    }
    void *p5 = kmalloc(65536);
    if (!p5) { put("  FAIL: legitimate 64 KiB alloc refused\n"); fail = 1; }
    else {
        memset(p5, 0xA5, 65536);
        kfree(p5);
    }

    if (!fail) { put("  PASS\n"); return 0; }
    put("  FAIL\n");
    return 1;
}

/* ---- heaptest (op 17) - v2.2.0 shell_cmd_heaptest, verbatim ----------- */
static int diag_heaptest(void) {
    put("Heap overhead test (100 allocs):\n");
    void *ptrs[100];
    int sizes[100];

    for (int i = 0; i < 100; i++) {
        sizes[i] = 16 + (i * 37) % 512;  /* 16 to 527 bytes */
        ptrs[i] = kmalloc(sizes[i]);
        if (!ptrs[i]) {
            put("  failed at alloc "); put_u64((u64)i); put("\n");
            break;
        }
        memset(ptrs[i], (u8)(i & 0xFF), sizes[i]);
    }

    mem_heap_stats_t hs;
    mem_heap_get_stats(&hs);
    put("  heap_size="); put_u64(hs.mem_heap_size);
    put(" allocated="); put_u64(hs.allocated);
    put(" overhead="); put_u64(hs.overhead);
    put("\n");
    u64 requested = 0;
    for (int i = 0; i < 100; i++) { if (ptrs[i]) requested += (u64)sizes[i]; }
    put("  requested="); put_u64(requested);
    put(" waste="); put_u64(hs.allocated - requested);
    u64 pct = hs.overhead * 100 / (hs.allocated + 1);
    put(" overhead%="); put_u64(pct);
    put("\n");

    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
        }
    }

    mem_heap_get_stats(&hs);
    put("  after free: alloc="); put_u64(hs.alloc_count);
    put(" free="); put_u64(hs.free_count);
    put(" total_allocs="); put_u64(hs.total_allocs);
    put(" total_frees="); put_u64(hs.total_frees);
    put("\n");
    put("  PASS\n");
    return 0;
}

/* ---- pmmrace (op 12) - v2.2.0 shell_cmd_pmmrace, verbatim ------------- */
#define PMMRACE_PAGES (131072)                     /* 512 MiB / 4 KiB */
#define PMMRACE_SHADOW_BYTES (PMMRACE_PAGES / 8)   /* 16 KiB */
static u8  *g_pmmrace_shadow = NULL;
static u64 g_pmmrace_hold[4];                      /* runs held by the callback */
static int g_pmmrace_active = 0;
static u64 g_pmmrace_cb_count = 0;
static volatile int g_pmmrace_in_alloc = 0;
static u64 g_pmmrace_overlaps = 0;
static u64 g_pmmrace_hold_fail = 0;
static u64 g_pmmrace_torn = 0;

static void pmmrace_shadow_set(u64 paddr, u64 count) {
    for (u64 k = 0; k < count; k++) {
        u64 idx = paddr >> 12;
        if (idx >= PMMRACE_PAGES) continue;
        g_pmmrace_shadow[idx >> 3] |= (u8)(1u << (idx & 7));
    }
}
static void pmmrace_shadow_clear(u64 paddr, u64 count) {
    for (u64 k = 0; k < count; k++) {
        u64 idx = paddr >> 12;
        if (idx >= PMMRACE_PAGES) continue;
        g_pmmrace_shadow[idx >> 3] &= (u8)~(1u << (idx & 7));
    }
}
static int pmmrace_shadow_test(u64 paddr, u64 count) {
    for (u64 k = 0; k < count; k++) {
        u64 idx = paddr >> 12;
        if (idx >= PMMRACE_PAGES) continue;
        if (g_pmmrace_shadow[idx >> 3] & (1u << (idx & 7))) return 1;
    }
    return 0;
}

static void pmmrace_pit_set(u16 divisor) {
    __asm__ volatile("outb %0, %1" :: "a"((u8)0x36), "Nd"(0x43));
    __asm__ volatile("outb %0, %1" :: "a"((u8)(divisor & 0xFF)), "Nd"(0x40));
    __asm__ volatile("outb %0, %1" :: "a"((u8)(divisor >> 8)), "Nd"(0x40));
}

static void pmmrace_timer_cb(void *ctx) {
    (void)ctx;
    g_pmmrace_cb_count++;
    if (!g_pmmrace_active) return;
    if (g_pmmrace_in_alloc) g_pmmrace_overlaps++;
    for (int k = 0; k < 4; k++) {
        if (g_pmmrace_hold[k]) {
            pmmrace_shadow_clear(g_pmmrace_hold[k], 400);
            for (u64 j = 0; j < 400; j++)
                mem_pmm_free_frame(g_pmmrace_hold[k] + (j << 12));
            g_pmmrace_hold[k] = 0;
        }
    }
    for (int k = 0; k < 4; k++) {
        u64 p = mem_pmm_alloc_contig(400);
        if (p) {
            g_pmmrace_hold[k] = p;
            pmmrace_shadow_set(p, 400);
        } else {
            g_pmmrace_hold_fail++;
        }
    }
}

static int diag_pmmrace(void) {
    static int registered = 0;
    if (!registered) {
        core_timer_register_periodic(pmmrace_timer_cb, NULL, 1);
        registered = 1;
    }
    if (!g_pmmrace_shadow) g_pmmrace_shadow = (u8 *)kmalloc(PMMRACE_SHADOW_BYTES);
    if (!g_pmmrace_shadow) { put("pmmrace: no memory\n"); return 1; }
    memset(g_pmmrace_shadow, 0, PMMRACE_SHADOW_BYTES);
    g_pmmrace_active = 1;
    pmmrace_pit_set(150);      /* ~7.9 kHz during the test */
    put("pmmrace: 20000 rounds of alloc_contig(512)+free "
        "vs IRQ-context allocator\n");
    int race = 0;
    int rounds = 0;
    for (int round = 0; round < 20000 && !race; round++) {
        rounds = round + 1;
        g_pmmrace_in_alloc = 1;
        u64 p = mem_pmm_alloc_contig(512);
        g_pmmrace_in_alloc = 0;
        for (int k = 0; k < 4; k++) {
            u64 h = g_pmmrace_hold[k];
            if (h && h >= p && h < p + (512u << 12)) {
                g_pmmrace_torn++;
            }
        }
        if (!p) { put("pmmrace: alloc failed\n"); break; }
        if (pmmrace_shadow_test(p, 512)) {
            put("pmmrace: RACE DETECTED at frame ");
            put_u64(p >> 12);
            put(" round "); put_u64((u64)rounds);
            put("\n");
            race = 1;
        } else {
            pmmrace_shadow_set(p, 512);
        }
        pmmrace_shadow_clear(p, 512);
        for (u64 j = 0; j < 512; j++) mem_pmm_free_frame(p + (j << 12));
    }
    for (int k = 0; k < 4; k++) {
        if (g_pmmrace_hold[k]) {
            pmmrace_shadow_clear(g_pmmrace_hold[k], 8);
            for (u64 j = 0; j < 8; j++)
                mem_pmm_free_frame(g_pmmrace_hold[k] + (j << 12));
            g_pmmrace_hold[k] = 0;
        }
    }
    g_pmmrace_active = 0;
    pmmrace_pit_set(11932);    /* restore 100 Hz */
    kfree(g_pmmrace_shadow);
    g_pmmrace_shadow = NULL;
    put("pmmrace: rounds="); put_u64((u64)rounds);
    put(" cb_count="); put_u64(g_pmmrace_cb_count);
    put(" window_overlaps="); put_u64(g_pmmrace_overlaps);
    put(" hold_fail="); put_u64(g_pmmrace_hold_fail);
    put(" torn="); put_u64(g_pmmrace_torn);
    put(" result=");
    put(race ? "FAIL (race)" : "PASS (atomic)");
    put("\n");
    return race;
}

/* ---- nice (op 24) - v2.2.0 shell_cmd_nice, verbatim ------------------- */
static int diag_nice(const char *args) {
    if (!args || !args[0]) {
        put("usage: nice <tid> <prio> (0=highest..31=lowest)\n");
        return 1;
    }
    int tid = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        int digit = args[i] - '0';
        if (tid > (0x7fffffff - digit) / 10) {
            put("nice: tid too large\n");
            return 1;
        }
        tid = tid * 10 + digit;
        i++;
    }
    if (i == 0) {
        put("nice: invalid tid\n");
        return 1;
    }
    while (args[i] == ' ') i++;
    int prio = -1;
    while (args[i] >= '0' && args[i] <= '9') {
        if (prio < 0) prio = 0;
        int digit = args[i] - '0';
        if (prio > 31) {
            put("nice: priority out of range (0..31)\n");
            return 1;
        }
        prio = prio * 10 + digit;
        i++;
    }
    if (prio < 0 || prio > 31) {
        put("nice: invalid tid or prio (0..31)\n");
        return 1;
    }
    if (core_kthread_set_priority(tid, prio) == 0) {
        put("nice: tid ");
        put_u64((u64)tid);
        put(" priority set to ");
        put_u64((u64)prio);
        put("\n");
        return 0;
    } else {
        put("nice: invalid tid (not in use)\n");
        return 1;
    }
}

/* ---- spawn (op 22) - v2.2.0 shell_cmd_spawn ---------------------------
 * The test thread's iteration lines print from kernel-thread context
 * straight to the console (v2.2.0 behavior - asynchronous, interleaved
 * with the prompt; same precedent as health watch's live view). */
static void diag_test_thread_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        screen_console_puts("  [test thread] iteration ");
        char n[20];
        u64_to_str((u64)i, n);
        screen_console_puts(n);
        screen_console_puts("\n");
        for (volatile int j = 0; j < 100000; j++);
        core_sched_yield();
    }
}
static int diag_spawn(void) {
    tid_t tid = core_kthread_create(diag_test_thread_fn, NULL, "test", TASK_PRIO_DEFAULT);
    if (tid >= 0) {
        put("spawned tid=");
        put_u64((u64)tid);
        put("\n");
    } else {
        put("spawn failed\n");
    }
    return 0;
}

/* ---- multi (op 23) - v2.2.0 shell_cmd_multi --------------------------- */
static void diag_multi_thread_fn(void *arg) {
    int id = (int)(u64)arg;
    for (int i = 0; i < 3; i++) {
        screen_console_puts("  [task ");
        char n[20];
        u64_to_str((u64)id, n);
        screen_console_puts(n);
        screen_console_puts("] iter ");
        u64_to_str((u64)i, n);
        screen_console_puts(n);
        screen_console_puts("\n");
        for (volatile int j = 0; j < 50000; j++);
        core_sched_yield();
    }
}
static int diag_multi(void) {
    put("Spawning 3 tasks...\n");
    core_kthread_create(diag_multi_thread_fn, (void*)1, "task1", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_multi_thread_fn, (void*)2, "task2", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_multi_thread_fn, (void*)3, "task3", TASK_PRIO_DEFAULT);
    put("3 tasks spawned (same priority). Output should interleave.\n");
    return 0;
}

/* ---- synctest (op 25) - v2.2.0 shell_cmd_synctest, verbatim ----------- */
static volatile u64 g_sync_counter = 0;
static spinlock_t g_sync_spin;
static mutex_t g_sync_mutex;
static sem_t g_sync_sem;
static volatile int g_sync_tasks_done = 0;

static void diag_sync_spin_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        spin_lock(&g_sync_spin);
        g_sync_counter++;
        spin_unlock(&g_sync_spin);
        core_sched_yield();
    }
    __sync_fetch_and_add(&g_sync_tasks_done, 1);
}

static void diag_sync_mutex_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        mutex_lock(&g_sync_mutex);
        g_sync_counter++;
        mutex_unlock(&g_sync_mutex);
        core_sched_yield();
    }
    __sync_fetch_and_add(&g_sync_tasks_done, 1);
}

static void diag_sync_sem_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        sem_wait(&g_sync_sem);
        __sync_fetch_and_add(&g_sync_counter, 1);  /* atomic increment */
        sem_post(&g_sync_sem);
        core_sched_yield();
    }
    __sync_fetch_and_add(&g_sync_tasks_done, 1);
}

static int diag_synctest(void) {
    /* Test 1: spinlock */
    put("=== Spinlock test (3 tasks x 1000) ===\n");
    g_sync_counter = 0;
    g_sync_tasks_done = 0;
    spin_init(&g_sync_spin);
    core_kthread_create(diag_sync_spin_fn, NULL, "spin1", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_sync_spin_fn, NULL, "spin2", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_sync_spin_fn, NULL, "spin3", TASK_PRIO_DEFAULT);
    while (g_sync_tasks_done < 3) core_sched_yield();
    put("  counter="); put_u64(g_sync_counter);
    put(" (expected 3000) ");
    put(g_sync_counter == 3000 ? "PASS" : "FAIL");
    put("\n");

    /* Test 2: mutex */
    put("=== Mutex test (3 tasks x 1000) ===\n");
    g_sync_counter = 0;
    g_sync_tasks_done = 0;
    mutex_init(&g_sync_mutex);
    core_kthread_create(diag_sync_mutex_fn, NULL, "mtx1", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_sync_mutex_fn, NULL, "mtx2", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_sync_mutex_fn, NULL, "mtx3", TASK_PRIO_DEFAULT);
    while (g_sync_tasks_done < 3) core_sched_yield();
    put("  counter="); put_u64(g_sync_counter);
    put(" (expected 3000) ");
    put(g_sync_counter == 3000 ? "PASS" : "FAIL");
    put("\n");

    /* Test 3: semaphore (binary = mutex) */
    put("=== Semaphore test (3 tasks x 1000) ===\n");
    g_sync_counter = 0;
    g_sync_tasks_done = 0;
    sem_init(&g_sync_sem, 1);  /* binary semaphore */
    core_kthread_create(diag_sync_sem_fn, NULL, "sem1", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_sync_sem_fn, NULL, "sem2", TASK_PRIO_DEFAULT);
    core_kthread_create(diag_sync_sem_fn, NULL, "sem3", TASK_PRIO_DEFAULT);
    while (g_sync_tasks_done < 3) core_sched_yield();
    put("  counter="); put_u64(g_sync_counter);
    put(" (expected 3000) ");
    put(g_sync_counter == 3000 ? "PASS" : "FAIL");
    put("\n");

    return 0;
}

/* ---- msitest (op 21) - v2.2.0 recovery/shell_cmds_chipset.c, verbatim - */
static u64 g_msi_test_vector = 0x50;
static void diag_msi_test_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
}

static int diag_msitest(void) {
    int fails = 0;

    /* 1. LAPIC presence + self-IPI (the exact message-write path) */
    if (arch_lapic_present() || arch_lapic_init() == 0) {
        put("LAPIC: present, id=");
        put_u64(arch_lapic_id());
        put("\n");

        u64 before = arch_lapic_vector_count((int)g_msi_test_vector);
        arch_lapic_register_vector((int)g_msi_test_vector, diag_msi_test_handler, NULL);
        arch_lapic_send_self_ipi((u8)g_msi_test_vector);
        for (volatile int i = 0; i < 200000; i++) { }   /* let it land */
        u64 after = arch_lapic_vector_count((int)g_msi_test_vector);
        put("  self-IPI vector 0x50: fired ");
        put_u64(after - before);
        put(" time(s)  ");
        put((after > before) ? "PASS\n" : "FAIL\n");
        if (after <= before) fails++;
    } else {
        put("LAPIC: not present            FAIL\n");
        fails++;
    }

    /* 2. MSI capability walk over the PCI cache */
    if (driver_pcie_count() == 0) driver_pcie_scan();
    int msi_devs = 0;
    for (int i = 0; i < driver_pcie_count(); i++) {
        const driver_pcie_dev_t *d = driver_pcie_get(i);
        if (d->cap_msi || d->cap_msix) msi_devs++;
    }
    put("MSI-capable devices: ");
    put_u64((u64)msi_devs);
    put(" of ");
    put_u64((u64)driver_pcie_count());
    put("\n");

    /* 3. real device fire: e1000 Interrupt Cause Set (ICS) raises a
     * genuine MSI message write when MSI is armed on the device. */
    u8 bus, dev, fn;
    int have_e1000 = driver_pci_find_device(0x8086, 0x100E, &bus, &dev, &fn) == 0;
    if (!have_e1000) have_e1000 = driver_pci_find_device(0x8086, 0x100F, &bus, &dev, &fn) == 0;
    if (have_e1000) {
        u32 bar0 = driver_pci_read_bar(bus, dev, fn, 0);
        driver_pcie_dev_t probe;
        memset(&probe, 0, sizeof(probe));
        probe.bus = bus; probe.dev = dev; probe.fn = fn;
        u32 status = driver_pcie_read_config(bus, dev, fn, 0x06);
        if (status & 0x10) {
            u8 ptr = (u8)(driver_pcie_read_config(bus, dev, fn, 0x34) & 0xFF);
            int guard = 0;
            while (ptr && ptr >= 0x40 && guard++ < 24) {
                u32 hdr = driver_pcie_read_config(bus, dev, fn, ptr);
                if ((hdr & 0xFF) == 0x05) { probe.cap_msi = ptr; break; }
                ptr = (u8)((hdr >> 8) & 0xFF);
            }
        }
        if (probe.cap_msi && bar0) {
            /* arm MSI on vector 0x51, fire via ICS, count, disarm */
            u8 v = 0x51;
            u64 before = arch_lapic_vector_count(v);
            arch_lapic_register_vector(v, diag_msi_test_handler, NULL);
            driver_pcie_msi_enable(&probe, v);
            driver_pci_enable_device(bus, dev, fn);
            volatile u32 *mmio = (volatile u32*)(uintptr_t)bar0;
            mmio[0x00C8 / 4] = 0xFFFFFFFFu;         /* ICS: force all causes */
            for (volatile int i = 0; i < 500000; i++) { }
            u64 after = arch_lapic_vector_count(v);
            driver_pcie_msi_disable(&probe);
            arch_lapic_unregister_vector(v, diag_msi_test_handler, NULL);
            put("  e1000 MSI fire (ICS->MSI): ");
            put_u64(after - before);
            put(" time(s)  ");
            put((after > before) ? "PASS\n" : "FAIL\n");
            if (after <= before) fails++;
        } else {
            put("  e1000: no MSI cap or BAR0  SKIP (reported honestly)\n");
        }
    } else {
        put("  e1000 not present: MSI device-fire skipped\n");
    }

    put(fails == 0 ? "msitest: PASS\n" : "msitest: FAIL\n");
    return fails;
}

/* ---- fstest (op 26) - v2.2.0 shell_cmd_fstest, verbatim --------------- */
static int diag_fstest(void) {
    put("VFS self-test:\n");
    /* mkdir /tmp/vfstest */
    if (fs_vfs_mkdir("/tmp/vfstest") < 0) {
        put("  mkdir /tmp/vfstest: FAIL (already exists?)\n");
    } else {
        put("  mkdir /tmp/vfstest: OK\n");
    }
    /* Write a file. */
    const char *text = "hello from ramfs";
    int fd = fs_vfs_open("/tmp/vfstest/hello.txt",
                      VFS_O_RDWR | VFS_O_CREAT);
    if (fd < 0) { put("  open(O_CREAT): FAIL\n"); return 0; }
    put("  open(O_CREAT): OK\n");
    int n = fs_vfs_write(fd, text, (int)strlen(text));
    if (n != (int)strlen(text)) { put("  write: FAIL\n"); }
    else put("  write: OK\n");
    fs_vfs_close(fd);

    /* Re-open and read back. */
    fd = fs_vfs_open("/tmp/vfstest/hello.txt", VFS_O_RDONLY);
    if (fd < 0) { put("  reopen: FAIL\n"); return 0; }
    char buf[64];
    n = fs_vfs_read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        put("  read: OK -> \"");
        put(buf);
        put("\"\n");
    } else {
        put("  read: FAIL\n");
    }
    fs_vfs_close(fd);

    /* List the directory. */
    put("  ls /tmp/vfstest:\n");
    for (int i = 0; ; i++) {
        fs_vfs_dirent_t e;
        if (fs_vfs_readdir("/tmp/vfstest", i, &e) < 0) break;
        put("    ");
        put(e.name);
        put("\n");
    }

    /* ramfs stats. */
    int nn, ss;
    fs_ramfs_get_stats(&nn, &ss);
    put("  ramfs: ");
    put_u64((u64)nn);
    put(" nodes, ");
    put_u64((u64)ss);
    put(" bytes\n");

    /* P2 regression block (BUG-0178/0179/0180/0182/0184) */
    {
        int p2fail = 0;
        put("  p2 regressions:\n");

        int wa = fs_vfs_open("/tmp/vfstest/p2_flags.txt",
                             VFS_O_RDWR | VFS_O_CREAT);
        if (wa < 0 || fs_vfs_write(wa, "x", 1) != 1) {
            put("    BUG-0178 legal O_RDWR write: FAIL\n");
            p2fail++;
        }
        fs_vfs_close(wa);
        int wb = fs_vfs_open("/tmp/vfstest/p2_flags.txt",
                             VFS_O_RDONLY | VFS_O_APPEND);
        if (wb >= 0) {
            if (fs_vfs_write(wb, "y", 1) >= 0) {
                put("    BUG-0178 RO|APPEND write refused: FAIL\n");
                p2fail++;
            } else {
                put("    BUG-0178 dual-sided: PASS\n");
            }
            fs_vfs_close(wb);
        } else {
            put("    BUG-0178 dual-sided: PASS (open refused)\n");
        }

        if (fs_vfs_open("/TMP/VFSTEST/HELLO.TXT", VFS_O_RDONLY) >= 0) {
            put("    BUG-0179 case-sensitive ramfs: FAIL\n");
            p2fail++;
        } else {
            put("    BUG-0179 dual-sided: PASS\n");
        }

        int hc = fs_vfs_open("/tmp/vfstest/p2_sparse.txt",
                             VFS_O_RDWR | VFS_O_CREAT);
        if (hc >= 0) {
            fs_vfs_seek(hc, 4096, 0 /* SEEK_SET */);
            fs_vfs_write(hc, "Z", 1);
            fs_vfs_seek(hc, 0, 0);
            char hole[64];
            int got = fs_vfs_read(hc, hole, sizeof(hole));
            int leak = 0;
            for (int i = 0; i < got; i++) if (hole[i] != 0) leak = 1;
            if (got == 64 && leak) {
                put("    BUG-0180 hole is zeros: FAIL\n");
                p2fail++;
            } else {
                put("    BUG-0180 hole is zeros: PASS\n");
            }
            fs_vfs_close(hc);
        }

        int pa = fs_vfs_open("/tmp/vfstest/p2_perm.txt",
                             VFS_O_RDWR | VFS_O_CREAT);
        if (pa >= 0) {
            fs_vfs_write(pa, "p", 1);
            fs_vfs_close(pa);
        }
        fs_vfs_chmod("/tmp/vfstest/p2_perm.txt", 0000);
        if (fs_vfs_open("/tmp/vfstest/p2_perm.txt", VFS_O_RDONLY) >= 0) {
            put("    BUG-0182 mode-0000 refused: FAIL\n");
            p2fail++;
        } else {
            put("    BUG-0182 dual-sided: PASS\n");
        }
        fs_vfs_chmod("/tmp/vfstest/p2_perm.txt", 0644);
        if (fs_vfs_open("/tmp/vfstest/p2_perm.txt", VFS_O_RDONLY) < 0) {
            put("    BUG-0182 chmod-644 restores: FAIL\n");
            p2fail++;
        }

        if (fs_vfs_umount("/") == 0) {
            put("    BUG-0184 umount / refused: FAIL\n");
            p2fail++;
        } else {
            put("    BUG-0184 umount / refused: PASS\n");
        }

        if (p2fail == 0) {
            put("    p2 regressions: ALL PASS\n");
        }
    }

    /* P1 fix4 regression block (BUG-0061) */
    {
        int p1f4fail = 0;
        put("  p1fix4 regressions:\n");

        int uf = fs_vfs_open("/tmp/vfstest/p1fix4_open.txt",
                             VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
        if (uf < 0) {
            put("    BUG-0061 setup open(O_CREAT): FAIL\n");
            p1f4fail++;
        } else {
            const char *utext = "p1fix4-still-alive";
            if (fs_vfs_write(uf, utext, (int)strlen(utext)) !=
                    (int)strlen(utext)) {
                put("    BUG-0061 setup write: FAIL\n");
                p1f4fail++;
            }
            int urc = fs_vfs_unlink("/tmp/vfstest/p1fix4_open.txt");
            if (urc != -4) {
                put("    BUG-0061 unlink-while-open busy: FAIL\n");
                p1f4fail++;
            } else {
                char ub[48];
                fs_vfs_seek(uf, 0, 0 /* SEEK_SET */);
                int un = fs_vfs_read(uf, ub, sizeof(ub) - 1);
                if (un >= 0) ub[un] = 0;   /* read does not NUL-terminate */
                int vf = fs_vfs_open("/tmp/vfstest/p1fix4_open.txt",
                                     VFS_O_RDONLY);
                char vb[48];
                int vn = -1;
                if (vf >= 0) {
                    vn = fs_vfs_read(vf, vb, sizeof(vb) - 1);
                    if (vn >= 0) vb[vn] = 0;
                    fs_vfs_close(vf);
                }
                if (un == (int)strlen(utext) && strcmp(ub, utext) == 0 &&
                    vn == (int)strlen(utext) && strcmp(vb, utext) == 0) {
                    put("    BUG-0061 dual-sided: PASS\n");
                } else {
                    put("    BUG-0061 file intact after refuse: FAIL\n");
                    p1f4fail++;
                }
            }
            fs_vfs_close(uf);
            if (fs_vfs_unlink("/tmp/vfstest/p1fix4_open.txt") != 0) {
                put("    BUG-0061 unlink-after-close: FAIL\n");
                p1f4fail++;
            } else if (fs_vfs_open("/tmp/vfstest/p1fix4_open.txt",
                                   VFS_O_RDONLY) >= 0) {
                put("    BUG-0061 file gone after unlink: FAIL\n");
                p1f4fail++;
            }
        }

        if (p1f4fail == 0) {
            put("    p1fix4 regressions: ALL PASS\n");
        }
    }
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
    /* G9: ring-0 self-test family */
    case L1SVC_DIAG_CR3TEST:   return diag_cr3test();
    case L1SVC_DIAG_EXC:       return diag_exc();
    case L1SVC_DIAG_PFTEST:    return diag_pftest();
    case L1SVC_DIAG_PMMRACE:   return diag_pmmrace();
    case L1SVC_DIAG_VMTEST:    return diag_vmtest();
    case L1SVC_DIAG_VMKERNELPT: return diag_vmkernelpt();
    case L1SVC_DIAG_VMMAP:     return diag_vmmap();
    case L1SVC_DIAG_MEMTEST:   return diag_memtest();
    case L1SVC_DIAG_HEAPTEST:  return diag_heaptest();
    case L1SVC_DIAG_HEAPBOUNDS: return diag_heapbounds();
    case L1SVC_DIAG_IRQABITEST: return diag_irqabitest();
    case L1SVC_DIAG_TIMER:     return diag_timer();
    case L1SVC_DIAG_MSITEST:   return diag_msitest();
    case L1SVC_DIAG_SPAWN:     return diag_spawn();
    case L1SVC_DIAG_MULTI:     return diag_multi();
    case L1SVC_DIAG_NICE:      return diag_nice(a);
    case L1SVC_DIAG_SYNCTEST:  return diag_synctest();
    case L1SVC_DIAG_FSTEST:    return diag_fstest();
    default: return -1;   /* ops 27-39: the G10 chipset group lands next */
    }
}

OC_SERVICE_DECL_L1("diag", "diag service (vitals + crash family + ring-0 self-tests + chipset diagnostics + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   diag_l1_dispatch);
