/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: shell/shell_cmds_wd.c
 * Purpose: the `watchdog` shell command - status/control of both the
 * hardware dog (TCO/WDAT) and the software monitored slots, plus the
 * three policy test hooks (spec 5.2 test: "a task stuck in a loop
 * that never feeds -> the soft dog fires PANIC or kills it").
 *
 * Subcommands:
 *   watchdog                    status overview (hw + slots)
 *   watchdog hw status          hardware probe/armed/timeout/base
 *   watchdog hw start <sec>     arm the hardware dog
 *   watchdog hw stop            halt it
 *   watchdog hw feed            kick it now
 *   watchdog hw timeout <sec>   set the countdown (2..63)
 *   watchdog slots              list every monitored slot
 *   watchdog start|stop|feed <id>          slot control
 *   watchdog timeout <id> <ms>  re-time a slot
 *   watchdog register <name> <ms> <policy> register a new slot
 *   watchdog test kill|panic|log
 *        spawn a kthread that registers ITSELF as monitored (3 s,
 *        the given policy) and then busy-loops WITHOUT feeding:
 *          kill  - the task is killed, kernel stays alive
 *          panic - the full WP-11 panic path fires
 *          log   - one ERROR is logged, no action
 */
#include "shell.h"
#include "shell_cmds_wd.h"
#include "screen_console.h"
#include "lib_string.h"
#include "lib_klog.h"
#include "core_watchdog.h"
#include "core_sched.h"
#include "core_timer.h"
#include "core_registry.h"

#pragma GCC optimize("Os")

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

/* ---- the self-registering starving test task ------------------------ */
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
        screen_console_puts("usage: watchdog test kill|panic|log\n");
        return 1;
    }
    tid_t tid = core_kthread_create(wd_test_task, (void*)(intptr_t)policy,
                                    "wd-starve", 15);
    if (tid < 0) { screen_console_puts("watchdog: task create FAILED\n"); return 1; }
    screen_console_puts("watchdog: wd-starve task started (tid ");
    char n[12];
    u64_to_str((u64)tid, n); screen_console_puts(n);
    screen_console_puts(") - registers itself monitored (3 s, ");
    screen_console_puts(wd_policy_name(policy));
    screen_console_puts(") and never feeds; expect the policy in 3 s\n");
    return 0;
}

/* ---- slot table ------------------------------------------------------ */
static int wd_print_slots(void) {
    char msg[160]; char n[24];
    strcpy(msg, "watchdog slots (");
    u64_to_str((u64)watchdog_count(), n); strcat(msg, n);
    strcat(msg, "):\n");
    screen_console_puts(msg);
    for (int i = 0; i < watchdog_count(); i++) {
        watchdog_info_t s;
        if (watchdog_get_info(i, &s) != 0) continue;
        strcpy(msg, "  [");
        u64_to_str((u64)i, n); strcat(msg, n);
        strcat(msg, "] ");
        strcat(msg, s.name);
        strcat(msg, "  ");
        u64_to_str((u64)s.timeout_ms, n); strcat(msg, n);
        strcat(msg, " ms  ");
        strcat(msg, wd_policy_name(s.policy));
        strcat(msg, "  ");
        strcat(msg, s.running ? "ARMED" : "stopped");
        strcat(msg, "  timeouts=");
        u64_to_str(s.timeouts, n); strcat(msg, n);
        strcat(msg, "\n");
        screen_console_puts(msg);
    }
    return 0;
}

int shell_cmd_watchdog(const char *args) {
    static char buf[96];
    buf[0] = 0;
    if (args && *args) { strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }

    /* split argv */
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
        char msg[160]; char n[24];
        strcpy(msg, "watchdog status:\n  hardware: ");
        strcat(msg, watchdog_hw_name());
        if (watchdog_hw_present()) {
            strcat(msg, " (");
            if (watchdog_hw_armed()) {
                strcat(msg, "ARMED, ");
                u64_to_str((u64)watchdog_hw_timeout(), n); strcat(msg, n);
                strcat(msg, " s");
            } else strcat(msg, "probed, halted");
            if (watchdog_hw_base()) {
                strcat(msg, ", base 0x");
                u64_to_hex(watchdog_hw_base(), n, 1); strcat(msg, n);
            }
            strcat(msg, ")");
        }
        strcat(msg, "\n");
        screen_console_puts(msg);
        return wd_print_slots();
    }

    if (strcmp(av[0], "hw") == 0) {
        const char *sub = (ac >= 2) ? av[1] : "status";
        if (strcmp(sub, "status") == 0) {
            char msg[160]; char n[24];
            if (!watchdog_hw_present()) {
                screen_console_puts("watchdog hw: none (no WDAT table, no Intel TCO LPC)\n");
                return 0;
            }
            strcpy(msg, "watchdog hw: ");
            strcat(msg, watchdog_hw_name());
            strcat(msg, " base=0x");
            u64_to_hex(watchdog_hw_base(), n, 1); strcat(msg, n);
            strcat(msg, " armed=");
            strcat(msg, watchdog_hw_armed() ? "yes" : "no");
            strcat(msg, " timeout=");
            u64_to_str((u64)watchdog_hw_timeout(), n); strcat(msg, n);
            strcat(msg, "s GCS.NO_REBOOT=");
            {
                int gcs = watchdog_hw_gcs_noreboot();
                char gb[12];
                if (gcs < 0) strcpy(gb, "n/a");
                else u64_to_str((u64)gcs, gb);
                strcat(msg, gb);
            }
            strcat(msg, "\n");
            screen_console_puts(msg);
            return 0;
        }
        if (strcmp(sub, "start") == 0) {
            long sec = 30;
            if (ac >= 3 && wd_num(av[2], &sec)) { /* given */ }
            int r = watchdog_hw_start((u32)sec);
            if (r == -1) { screen_console_puts("watchdog hw: no hardware watchdog present\n"); return 1; }
            if (r != 0) { screen_console_puts("watchdog hw: start FAILED (bad timeout?)\n"); return 1; }
            screen_console_puts("watchdog hw: ARMED - machine RESETS if nobody feeds\n");
            return 0;
        }
        if (strcmp(sub, "stop") == 0) {
            int r = watchdog_hw_stop();
            if (r != 0) { screen_console_puts("watchdog hw: stop FAILED\n"); return 1; }
            screen_console_puts("watchdog hw: halted\n");
            return 0;
        }
        if (strcmp(sub, "feed") == 0) {
            int r = watchdog_hw_feed();
            if (r != 0) { screen_console_puts("watchdog hw: feed FAILED (not armed?)\n"); return 1; }
            screen_console_puts("watchdog hw: fed\n");
            return 0;
        }
        if (strcmp(sub, "timeout") == 0) {
            long sec;
            if (ac < 3 || !wd_num(av[2], &sec) || sec < 2 || sec > 63) {
                screen_console_puts("usage: watchdog hw timeout <2..63 seconds>\n");
                return 1;
            }
            int r = watchdog_hw_set_timeout((u32)sec);
            if (r != 0) { screen_console_puts("watchdog hw: set FAILED\n"); return 1; }
            screen_console_puts("watchdog hw: timeout set\n");
            return 0;
        }
        screen_console_puts("usage: watchdog hw status|start|stop|feed|timeout\n");
        return 1;
    }

    if (strcmp(av[0], "slots") == 0) return wd_print_slots();

    if (strcmp(av[0], "start") == 0 || strcmp(av[0], "stop") == 0 ||
        strcmp(av[0], "feed") == 0) {
        long id;
        if (ac < 2 || !wd_num(av[1], &id)) {
            screen_console_puts("usage: watchdog start|stop|feed <id>\n");
            return 1;
        }
        int r = (av[0][0] == 's' && av[0][1] == 't') ? watchdog_start((int)id)
                : (av[0][0] == 's') ? watchdog_stop((int)id)
                : watchdog_feed((int)id);
        if (r != 0) { screen_console_puts("watchdog: bad slot id\n"); return 1; }
        screen_console_puts("watchdog: ok\n");
        return 0;
    }

    if (strcmp(av[0], "timeout") == 0) {
        long id, ms;
        if (ac < 3 || !wd_num(av[1], &id) || !wd_num(av[2], &ms) || ms < 100 || ms > 3600000) {
            screen_console_puts("usage: watchdog timeout <id> <ms 100..3600000>\n");
            return 1;
        }
        int r = watchdog_set_timeout((int)id, (u32)ms);
        if (r != 0) { screen_console_puts("watchdog: bad slot id\n"); return 1; }
        screen_console_puts("watchdog: timeout set\n");
        return 0;
    }

    if (strcmp(av[0], "register") == 0) {
        long ms, pol;
        if (ac < 4 || !av[1][0] || !wd_num(av[2], &ms) || !wd_num(av[3], &pol) ||
            pol < 0 || pol > 3 || ms < 100 || ms > 3600000) {
            screen_console_puts("usage: watchdog register <name> <ms> <0=panic 1=reboot 2=kill 3=log>\n");
            return 1;
        }
        int id = watchdog_register(av[1], (u32)ms, (int)pol);
        if (id < 0) { screen_console_puts("watchdog: register FAILED (table full?)\n"); return 1; }
        char msg[80]; char n[12];
        strcpy(msg, "watchdog: slot ");
        u64_to_str((u64)id, n); strcat(msg, n);
        strcat(msg, " registered (stopped; `watchdog start ");
        strcat(msg, n);
        strcat(msg, "`)\n");
        screen_console_puts(msg);
        return 0;
    }

    if (strcmp(av[0], "test") == 0) {
        if (ac < 2) return wd_test("");
        return wd_test(av[1]);
    }

    screen_console_puts("usage: watchdog [status|slots|hw ...|start|stop|feed|timeout|register|test]\n");
    return 1;
}


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
OC_SHELL_CMD("watchdog", shell_cmd_watchdog, "watchdog status/control (hw TCO/WDAT + monitored slots + policy tests)", "watchdog");
