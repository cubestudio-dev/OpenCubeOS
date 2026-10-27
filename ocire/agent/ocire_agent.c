/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/agent/ocire_agent.c
 * Purpose: the OCIRE log agent - a ring-3 process the OCIRE boot
 *          supervisor injects into OCOS as a multiboot2 module
 *          (kexec hand-over) and OCOS spawns as a PROTECTED process
 *          (task ocire_guard: kill/nice/job-control all refused,
 *          kernel layer 1; the agent binary itself lives on the
 *          OCIRE recovery volume where OCOS cannot touch it, layer 2).
 *
 *          The agent is the recovery side's eyes inside a running
 *          OCOS: it periodically snapshots the kernel log ring and
 *          appends it (plus its own heartbeat) to the boot log on
 *          the OCIRE volume through the kernel-only OCIRE channel.
 *          It never exits and never needs anything beyond two
 *          SYS_L1SVC calls - by design there is no code path that
 *          could turn it into a general "access OCIRE" door: the
 *          kernel service checks the caller's ocire_guard flag.
 *
 *          This file is OCIRE's asset but compiles against the OCOS
 *          L1 ABI (int 0x80 syscalls, ET_EXEC @ 0x400000, same
 *          recipe as l1apps/) - an ABI, not a source dependency.
 */

typedef unsigned long u64;

#define SYS_WRITE  1
#define SYS_L1SVC  110

/* services/l1/l1svc_defs.h numbering (verbatim contract). */
#define L1SVC_CAT_POWER   5
#define L1SVC_PWR_SLEEP   4
#define L1SVC_CAT_OCIRE  18
#define L1SVC_OCIRE_LOG_APPEND   4   /* guard-only: append to the boot log */
#define L1SVC_OCIRE_KLOG_SNAPSHOT 5 /* guard-only: kernel log ring text */

static inline long syscall5(long n, long a, long b, long c, long d, long e) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c),
                     "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return r;
}

static inline long syscall3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

static void wr(const char *s) {
    long n = 0;
    while (s[n]) n++;
    syscall3(SYS_WRITE, 1, (long)s, n);
}

static void append_log(const char *text) {
    /* out[0] receives a one-line status; the payload is `text`. */
    char out[64];
    syscall5(SYS_L1SVC, L1SVC_CAT_OCIRE, L1SVC_OCIRE_LOG_APPEND,
             (long)text, (long)out, (long)sizeof(out));
}

void _start(void) {
    static char klog[4096];

    wr("[ocire-agent] started; capturing the OCOS kernel log\n");
    append_log("=== ocire-agent started ===\n");

    for (;;) {
        /* Sleep 5 s through the power service (L1SVC sleep <sec>). */
        syscall5(SYS_L1SVC, L1SVC_CAT_POWER, L1SVC_PWR_SLEEP,
                 (long)"5", (long)0, (long)0);

        /* Snapshot the kernel log ring (guard-only op). */
        long rc = syscall5(SYS_L1SVC, L1SVC_CAT_OCIRE,
                           L1SVC_OCIRE_KLOG_SNAPSHOT,
                           (long)0, (long)klog, (long)sizeof(klog));
        if (rc == 0) {
            append_log(klog);
        } else {
            append_log("=== ocire-agent: klog snapshot refused ===\n");
        }
        /* Heartbeat line so the log shows the agent is alive even
         * when the kernel ring is quiet. */
        append_log("=== ocire-agent alive ===\n");
    }
    /* never returns */
}

/* Keep gcc from complaining about a missing main in freestanding
 * builds: _start is the entry (user.ld). */
int main(void) { return 0; }
