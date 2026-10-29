/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/watchdog ring-3 surface.
 *
 * The v2.2.0 L0 command `watchdog` (recovery/shell_cmds_wd.c) migrates
 * to L1.  The category string "watchdog" (reserved as category 11 since
 * v2.0.0) equals this folder name; the hardware and monitored-slot
 * mechanisms live in services/crash/core_watchdog.* and are only
 * consumed here.  The one op carries the whole subcommand line in arg
 * and dispatches it exactly as the v2.2.0 shell command body did -
 * subcommand words are arguments of the single registered command
 * name.  Output is rendered into the caller's buffer. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "core_watchdog.h"
#include "core_sched.h"
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
static void put_hex(u64 v, int min_digits) {
    char b[24];
    u64_to_hex(v, b, min_digits);
    put(b);
}

static int wd_num(const char *s, long *out) {
    if (!s || !*s) return 0;
    long v = 0; int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
        if (v > 100000000) return 0;
    }
    *out = neg ? -v : v;
    return 1;
}

static const char *wd_policy_name(int p) {
    switch (p) {
    case WD_POLICY_PANIC:  return "PANIC";
    case WD_POLICY_REBOOT: return "REBOOT";
    case WD_POLICY_KILL:   return "KILL";
    case WD_POLICY_LOG:    return "LOG";
    default:               return "?";
    }
}

static int wd_policy_of(const char *s) {
    if (strcasecmp(s, "panic")  == 0) return WD_POLICY_PANIC;
    if (strcasecmp(s, "reboot") == 0) return WD_POLICY_REBOOT;
    if (strcasecmp(s, "kill")   == 0) return WD_POLICY_KILL;
    if (strcasecmp(s, "log")    == 0) return WD_POLICY_LOG;
    return -1;
}

static void wd_test_task(void *arg) {
    int policy = (int)(long)(intptr_t)arg;
    int id = watchdog_register("wd-starve", 3000, policy);
    if (id < 0) return;
    watchdog_start(id);
    /* the loop the spec asks for: busy, NEVER feeds.  The scheduler
     * still preempts us at 100 Hz, so the 1 s watchdog tick keeps
     * running and fires the policy 3 s later. */
    for (;;) { __asm__ volatile("pause"); }
}

static int wd_test(const char *which) {
    int policy = wd_policy_of(which);
    if (policy < 0) {
        put("usage: watchdog test kill|panic|log\n");
        return 1;
    }
    tid_t tid = core_kthread_create(wd_test_task, (void*)(intptr_t)policy,
                                    "wd-starve", 15);
    if (tid < 0) { put("watchdog: task create FAILED\n"); return 1; }
    put("watchdog: wd-starve task started (tid ");
    put_u64((u64)tid);
    put(") - registers itself monitored (3 s, ");
    put(wd_policy_name(policy));
    put(") and never feeds; expect the policy in 3 s\n");
    return 0;
}

static int wd_print_slots(void) {
    put("watchdog slots (");
    put_u64((u64)watchdog_count());
    put("):\n");
    for (int i = 0; i < watchdog_count(); i++) {
        watchdog_info_t s;
        if (watchdog_get_info(i, &s) != 0) continue;
        put("  [");
        put_u64((u64)i);
        put("] ");
        put(s.name);
        put("  ");
        put_u64((u64)s.timeout_ms);
        put(" ms  ");
        put(wd_policy_name(s.policy));
        put("  ");
        put(s.running ? "ARMED" : "stopped");
        put("  timeouts=");
        put_u64(s.timeouts);
        put("\n");
    }
    return 0;
}

/* The v2.2.0 shell_cmd_watchdog() body, verbatim semantics; the
 * console writes became buffer appends. */
static int watchdog_cmd_core(const char *args) {
    static char buf[96];
    buf[0] = 0;
    if (args && *args) { strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }

    const char *av[5];
    int ac = 0;
    char *p = buf;
    while (*p && ac < 5) {
        while (*p == ' ') p++;
        if (!*p) break;
        av[ac++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }

    if (ac == 0 || strcmp(av[0], "status") == 0) {
        put("watchdog status:\n  hardware: ");
        put(watchdog_hw_name());
        if (watchdog_hw_present()) {
            put(" (");
            if (watchdog_hw_armed()) {
                put("ARMED, ");
                put_u64((u64)watchdog_hw_timeout());
                put(" s");
            } else put("probed, halted");
            if (watchdog_hw_base()) {
                put(", base 0x");
                put_hex(watchdog_hw_base(), 1);
            }
            put(")");
        }
        put("\n");
        return wd_print_slots();
    }

    if (strcmp(av[0], "hw") == 0) {
        const char *sub = (ac >= 2) ? av[1] : "status";
        if (strcmp(sub, "status") == 0) {
            if (!watchdog_hw_present()) {
                put("watchdog hw: none (no WDAT table, no Intel TCO LPC)\n");
                return 0;
            }
            put("watchdog hw: ");
            put(watchdog_hw_name());
            put(" base=0x");
            put_hex(watchdog_hw_base(), 1);
            put(" armed=");
            put(watchdog_hw_armed() ? "yes" : "no");
            put(" timeout=");
            put_u64((u64)watchdog_hw_timeout());
            put("s GCS.NO_REBOOT=");
            {
                int gcs = watchdog_hw_gcs_noreboot();
                if (gcs < 0) put("n/a");
                else put_u64((u64)gcs);
            }
            put("\n");
            return 0;
        }
        if (strcmp(sub, "start") == 0) {
            long sec = 30;
            if (ac >= 3 && wd_num(av[2], &sec)) { /* given */ }
            int r = watchdog_hw_start((u32)sec);
            if (r == -1) { put("watchdog hw: no hardware watchdog present\n"); return 1; }
            if (r != 0) { put("watchdog hw: start FAILED (bad timeout?)\n"); return 1; }
            put("watchdog hw: ARMED - machine RESETS if nobody feeds\n");
            return 0;
        }
        if (strcmp(sub, "stop") == 0) {
            int r = watchdog_hw_stop();
            if (r != 0) { put("watchdog hw: stop FAILED\n"); return 1; }
            put("watchdog hw: halted\n");
            return 0;
        }
        if (strcmp(sub, "feed") == 0) {
            int r = watchdog_hw_feed();
            if (r != 0) { put("watchdog hw: feed FAILED (not armed?)\n"); return 1; }
            put("watchdog hw: fed\n");
            return 0;
        }
        if (strcmp(sub, "timeout") == 0) {
            long sec;
            if (ac < 3 || !wd_num(av[2], &sec) || sec < 2 || sec > 63) {
                put("usage: watchdog hw timeout <2..63 seconds>\n");
                return 1;
            }
            int r = watchdog_hw_set_timeout((u32)sec);
            if (r != 0) { put("watchdog hw: set FAILED\n"); return 1; }
            put("watchdog hw: timeout set\n");
            return 0;
        }
        put("usage: watchdog hw status|start|stop|feed|timeout\n");
        return 1;
    }

    if (strcmp(av[0], "slots") == 0) return wd_print_slots();

    if (strcmp(av[0], "start") == 0 || strcmp(av[0], "stop") == 0 ||
        strcmp(av[0], "feed") == 0) {
        long id;
        if (ac < 2 || !wd_num(av[1], &id)) {
            put("usage: watchdog start|stop|feed <id>\n");
            return 1;
        }
        int r = (av[0][0] == 's' && av[0][1] == 't') ? watchdog_start((int)id)
                : (av[0][0] == 's') ? watchdog_stop((int)id)
                : watchdog_feed((int)id);
        if (r != 0) { put("watchdog: bad slot id\n"); return 1; }
        put("watchdog: ok\n");
        return 0;
    }

    if (strcmp(av[0], "timeout") == 0) {
        long id, ms;
        if (ac < 3 || !wd_num(av[1], &id) || !wd_num(av[2], &ms) || ms < 100 || ms > 3600000) {
            put("usage: watchdog timeout <id> <ms 100..3600000>\n");
            return 1;
        }
        int r = watchdog_set_timeout((int)id, (u32)ms);
        if (r != 0) { put("watchdog: bad slot id\n"); return 1; }
        put("watchdog: timeout set\n");
        return 0;
    }

    if (strcmp(av[0], "register") == 0) {
        long ms, pol;
        if (ac < 4 || !av[1][0] || !wd_num(av[2], &ms) || !wd_num(av[3], &pol) ||
            pol < 0 || pol > 3 || ms < 100 || ms > 3600000) {
            put("usage: watchdog register <name> <ms> <0=panic 1=reboot 2=kill 3=log>\n");
            return 1;
        }
        int id = watchdog_register(av[1], (u32)ms, (int)pol);
        if (id < 0) { put("watchdog: register FAILED (table full?)\n"); return 1; }
        put("watchdog: slot ");
        put_u64((u64)id);
        put(" registered (stopped; `watchdog start ");
        put_u64((u64)id);
        put("`)\n");
        return 0;
    }

    if (strcmp(av[0], "test") == 0) {
        if (ac < 2) return wd_test("");
        return wd_test(av[1]);
    }

    put("usage: watchdog [status|slots|hw ...|start|stop|feed|timeout|register|test]\n");
    return 1;
}

static int watchdog_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    switch (op) {
    case L1SVC_WDT_CMD:
        return watchdog_cmd_core(arg ? arg : "");
    default:
        return -1;
    }
}

OC_SERVICE_DECL_L1("watchdog", "watchdog service (hw TCO/WDAT + monitored slots + policy + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   watchdog_l1_dispatch);
