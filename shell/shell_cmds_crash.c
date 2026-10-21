/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-11-crash_recovery
 * File: shell/shell_cmds_crash.c
 * Purpose: the three crash-recovery shell commands (crashlog completion,
 *          crashtest, dumpinfo) - see kernel/core/core_crash.h for the
 *          machinery they drive.
 *
 * Size note: registered as a WP-11 shell module (same pattern as
 * shell_cmds_disk_test etc.) and compiled for SIZE: the command table
 * registration moved out of main.c so this file could take
 * #pragma GCC optimize("Os") without touching any hot path (these are
 * interactive one-shot commands).
 */
#include "shell.h"
#include "shell_cmds_crash.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_crash.h"
#include "core_sched.h"    /* WP-11: crashtest ovf burns a kthread stack */

#pragma GCC optimize("Os")

/* WP-11 4.4: crashtest ovf - burn a dedicated kthread's stack with a
 * 4 KiB-frame recursion until it hits the guard page (unmapped lowest
 * stack page, core_kthread_create). The #PF lands in ring-0, the
 * dispatcher recognises the guard page and panics with
 * "KERNEL STACK OVERFLOW (guard page hit, task=kstack-ovf)". Runs in
 * its own task so the shell's own stack is never the one burning. */
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


/* WP-11-crash_recovery: crashlog — formatted listing of the persistent
 * crash log ring (kernel/core/core_crash.c). Supersedes the WP-04
 * 8-entry raw dump:
 *   crashlog           - list every recorded entry (oldest first)
 *   crashlog flush     - append the ring to /var/log/crash.log
 * The command NAME is unchanged (WP-04 compat); the output format and
 * the flush subcommand are the WP-11 completion the spec asked for. */
int shell_cmd_crash_crashlog(const char *args) {
    while (args && *args == ' ') args++;
    if (args && strcmp(args, "flush") == 0) {
        int n = crashlog_flush();
        if (n < 0) {
            screen_console_puts("crashlog: flush FAILED (no filesystem mounted?)\n");
            return 1;
        }
        char buf[80]; char n2[12];
        strcpy(buf, "crashlog: flushed ");
        u64_to_str((u64)n, n2); strcat(buf, n2);
        strcat(buf, " bytes to /var/log/crash.log\n");
        screen_console_puts(buf);
        return 0;
    }
    int count = crashlog_count();
    if (count == 0) {
        screen_console_puts("crashlog: no exceptions recorded\n");
        return 0;
    }
    char buf[200]; char n[20];
    strcpy(buf, "crashlog: "); u64_to_str((u64)count, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " entries\n"); screen_console_puts(buf);
    for (int i = 0; i < count; i++) {
        const crashlog_entry_t *e = crashlog_get(i);
        if (!e) break;
        strcpy(buf, "  ["); u64_to_str((u64)i, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "] ");
        strcpy(buf+strlen(buf), crash_src_name(e->vector));
        strcpy(buf+strlen(buf), " ring="); u64_to_str(e->ring, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " err=0x"); u64_to_hex(e->error_code, n, 0);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " rip=0x"); u64_to_hex(e->rip, n, 0);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " rsp=0x"); u64_to_hex(e->rsp, n, 0);
        strcpy(buf+strlen(buf), n);
        if (e->cr2) {
            strcpy(buf+strlen(buf), " cr2=0x");
            u64_to_hex(e->cr2, n, 0); strcpy(buf+strlen(buf), n);
        }
        if (e->rtc_epoch) {
            strcpy(buf+strlen(buf), " @");
            u64_to_str(e->rtc_epoch, n); strcpy(buf+strlen(buf), n);
            strcpy(buf+strlen(buf), "s");
        } else {
            strcpy(buf+strlen(buf), " @tick=");
            u64_to_str(e->jiffies, n); strcpy(buf+strlen(buf), n);
        }
        if (e->task[0]) {
            strcpy(buf+strlen(buf), " task=");
            strcpy(buf+strlen(buf), e->task);
        }
        if (e->pid > 0) {
            strcpy(buf+strlen(buf), " pid=");
            u64_to_str((u64)e->pid, n); strcpy(buf+strlen(buf), n);
        }
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    }
    return 0;
}

/* WP-11-crash_recovery: dumpinfo — inspect the current core-dump state.
 * Shows whether a dump exists, its size, where it was persisted, the
 * header summary (version/build/reason/task/registers/stack window)
 * plus the live recovery policy and watchdog state. */
int shell_cmd_crash_dumpinfo(const char *args) {
    (void)args;
    char buf[160]; char n[24];
    screen_console_puts("core dump status:\n");

    u32 size = core_dump_size();
    if (size == 0) {
        screen_console_puts("  no dump captured yet (trigger: crashtest panic|exc|pf)\n");
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
        strcpy(buf, "  image: "); u64_to_str(total, n); strcat(buf, n);
        strcat(buf, " bytes, magic ");
        strcat(buf, magic_ok ? "OCDUMP01 OK" : "BAD");
        strcat(buf, ", ");
        strcat(buf, core_dump_persisted()
                        ? "persisted to /var/crash/core.dump"
                        : "serial-only (no filesystem)");
        strcat(buf, "\n");
        screen_console_puts(buf);

        if (magic_ok) {
            strcpy(buf, "  build: "); u64_to_str(h->build, n);
            strcat(buf, n);
            strcat(buf, "  version: ");
            strncpy(buf + strlen(buf), h->version, 23);
            buf[strlen(buf)] = 0;
            strcat(buf, "\n");
            screen_console_puts(buf);
            strcpy(buf, "  reason: ");
            strncpy(buf + strlen(buf), h->reason, 39);
            buf[strlen(buf)] = 0;
            strcat(buf, "\n");
            screen_console_puts(buf);
            strcpy(buf, "  task: ");
            strncpy(buf + strlen(buf), h->task, 15);
            buf[strlen(buf)] = 0;
            strcpy(buf+strlen(buf), " tid=");
            u64_to_str(h->tid, n); strcat(buf, n);
            strcpy(buf+strlen(buf), " regs=");
            strcat(buf, h->regs_present ? "exception-frame" : "live-snapshot");
            strcpy(buf+strlen(buf), " stack=");
            u64_to_str(h->stack_copy_len, n); strcat(buf, n);
            strcpy(buf+strlen(buf), "/");
            u64_to_str(h->stack_size, n); strcat(buf, n);
            strcpy(buf+strlen(buf), "B\n");
            screen_console_puts(buf);
        }
    }

    strcpy(buf, "policy: ");
    strcat(buf, crash_recovery_policy_name(crash_recovery_get_policy()));
    strcat(buf, "  watchdog: ");
    strcat(buf, crash_wdt_armed() ? "armed" : "off");
    strcat(buf, " (timeout 15000 ms)\n");
    screen_console_puts(buf);
    return 0;
}

/* WP-11-crash_recovery: crashtest — manual kernel-crash triggers so the
 * recovery machinery can be exercised on demand:
 *   crashtest panic          - kernel_panic() direct (policy applies)
 *   crashtest exc            - ring-0 #UD -> exception -> panic path
 *   crashtest pf             - ring-0 NULL write -> #PF -> panic path
 *   crashtest assert         - OC_ASSERT(0) failure
 *   crashtest wd             - forced watchdog timeout (real tick path)
 *   crashtest dump           - save a dump WITHOUT panicking
 *   crashtest policy [halt|reboot]
 * Every panic variant prints the full banner + registers + traceback,
 * saves the dump, flushes the log, then HALTs or REBOOTs per policy. */
int shell_cmd_crash_crashtest(const char *args) {
    while (args && *args == ' ') args++;
    if (!args || !args[0] || strcmp(args, "help") == 0) {
        screen_console_puts(
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
        char buf[80];
        strcpy(buf, "crashtest: policy = ");
        strcat(buf, crash_recovery_policy_name(crash_recovery_get_policy()));
        strcat(buf, "\n");
        screen_console_puts(buf);
        return 0;
    }
    if (strncmp(args, "policy ", 7) == 0) {
        const char *v = args + 7;
        while (*v == ' ') v++;
        int p = -1;
        if (strcmp(v, "halt") == 0 || strcmp(v, "0") == 0) p = CRASH_POLICY_HALT;
        if (strcmp(v, "reboot") == 0 || strcmp(v, "1") == 0) p = CRASH_POLICY_REBOOT;
        if (p < 0) {
            screen_console_puts("crashtest: policy must be halt or reboot\n");
            return 1;
        }
        crash_recovery_set_policy(p);
        screen_console_puts("crashtest: policy set - after the next panic the kernel will ");
        screen_console_puts(p == CRASH_POLICY_REBOOT ? "REBOOT\n" : "HALT\n");
        return 0;
    }
    if (strcmp(args, "panic") == 0) {
        screen_console_puts("crashtest: triggering kernel_panic()...\n");
        kernel_panic("manual crashtest: kernel_panic()");
        return 0; /* not reached when policy=halt */
    }
    if (strcmp(args, "exc") == 0) {
        screen_console_puts("crashtest: triggering ring-0 #UD...\n");
        __asm__ volatile(".byte 0x0F, 0x0B\n");
        screen_console_puts("crashtest: #UD returned unexpectedly\n");
        return 1;
    }
    if (strcmp(args, "pf") == 0) {
        screen_console_puts("crashtest: triggering ring-0 unmapped write...\n");
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
        screen_console_puts("crashtest: unmapped write returned unexpectedly\n");
        return 1;
    }
    if (strcmp(args, "assert") == 0) {
        screen_console_puts("crashtest: triggering OC_ASSERT(0)...\n");
        OC_ASSERT(0);
        screen_console_puts("crashtest: assert returned unexpectedly\n");
        return 1;
    }
    if (strcmp(args, "wd") == 0) {
        screen_console_puts("crashtest: forcing watchdog timeout (armed + rewound heartbeat)...\n");
        extern void crash_wdt_force_timeout(void);
        crash_wdt_force_timeout();
        screen_console_puts("crashtest: waiting for the next watchdog tick...\n");
        for (;;) { __asm__ volatile("sti"); __asm__ volatile("hlt"); }
        return 0;
    }
    if (strcmp(args, "ovf") == 0) {
        screen_console_puts("crashtest: spawning kstack-ovf task (4 KiB frames until the guard page)...\n");
        tid_t tid = core_kthread_create(crashtest_ovf_task, NULL,
                                        "kstack-ovf", 20);
        if (tid < 0) {
            screen_console_puts("crashtest: task creation FAILED\n");
            return 1;
        }
        screen_console_puts("crashtest: kstack-ovf task started - expect a guard-page panic\n");
        return 0;
    }
    if (strcmp(args, "dump") == 0) {
        int n = core_dump_save("manual crashtest dump", NULL);
        char buf[96]; char n2[12];
        if (n < 0) {
            screen_console_puts("crashtest: dump FAILED (no boot-time buffer)\n");
            return 1;
        }
        strcpy(buf, "crashtest: captured "); u64_to_str((u64)n, n2);
        strcat(buf, n2);
        strcat(buf, " bytes, ");
        strcat(buf, core_dump_persisted() ? "persisted to /var/crash/core.dump"
                                          : "serial-only (no filesystem)");
        strcat(buf, " - see dumpinfo\n");
        screen_console_puts(buf);
        return 0;
    }
    screen_console_puts("crashtest: unknown variant '");
    screen_console_puts(args);
    screen_console_puts("' (try: crashtest help)\n");
    return 1;
}

/* registration (called from kmain after core_crash_init) */
void shell_cmds_crash_register(void) {
    shell_register_command_ex("crashlog", shell_cmd_crash_crashlog,
                              "show last exception crashes", "WP-04");
    shell_register_command_ex("crashtest", shell_cmd_crash_crashtest,
                              "trigger kernel crash tests (panic/exc/pf/assert/wd/dump/policy)", "WP-11");
    shell_register_command_ex("dumpinfo", shell_cmd_crash_dumpinfo,
                              "show core dump status (size/persisted/header)", "WP-11");
}
