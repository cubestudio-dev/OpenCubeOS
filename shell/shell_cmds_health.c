/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: shell/shell_cmds_health.c
 * Purpose: the `health` command - current status, live watch and the
 * JSON export (spec 5.7).
 *
 *   health             one full snapshot (all metric groups + alarms)
 *   health watch       refresh every second until ENTER
 *   health export [p]  write the JSON object to a file
 *                      (default /var/log/health.json) and print it
 */
#include "shell.h"
#include "shell_cmds_health.h"
#include "screen_console.h"
#include "lib_string.h"
#include "lib_klog.h"
#include "core_health.h"
#include "core_timer.h"
#include "fs_vfs.h"
#include "core_registry.h"

#pragma GCC optimize("Os")

static void hc_line(const char *a, u64 v, const char *unit) {
    char msg[96]; char n[24];
    strcpy(msg, "  ");
    strcat(msg, a);
    while (strlen(msg) < 22) strcat(msg, " ");
    u64_to_str(v, n); strcat(msg, n);
    if (unit && *unit) { strcat(msg, " "); strcat(msg, unit); }
    strcat(msg, "\n");
    screen_console_puts(msg);
}

static int health_print(void) {
    health_sample_t s;
    /* fresh sample (not the cached one) so `health` is live */
    extern int health_get_cpu(u32 *busy_pct);
    extern int health_get_mem(u64 *t, u64 *u, u64 *f);
    extern int health_get_tasks(u32 *t, u32 *r, u32 *rdy, u32 *b);
    extern int health_get_io(u64 *dr, u64 *dw, u64 *nt, u64 *nr);
    u32 cpu = 0; u64 mt = 0, mu = 0, mf = 0;
    u32 tt = 0, tr = 0, trdy = 0, tb = 0;
    u64 dr = 0, dw = 0, nt = 0, nr = 0;
    health_get_cpu(&cpu);
    health_get_mem(&mt, &mu, &mf);
    health_get_tasks(&tt, &tr, &trdy, &tb);
    health_get_io(&dr, &dw, &nt, &nr);
    (void)s;

    char msg[128];
    screen_console_puts("system health:\n");
    screen_console_puts("  cpu:\n");
    hc_line("busy", cpu, "% (1 s window)");
    screen_console_puts("  memory:\n");
    hc_line("total", mt, "KiB");
    hc_line("used", mu, "KiB");
    hc_line("free", mf, "KiB");
    screen_console_puts("  tasks:\n");
    hc_line("total", tt, "");
    hc_line("running", tr, "");
    hc_line("ready", trdy, "");
    hc_line("blocked", tb, "");
    screen_console_puts("  disk:\n");
    hc_line("read", dr, "bytes");
    hc_line("written", dw, "bytes");
    screen_console_puts("  network:\n");
    hc_line("tx", nt, "bytes");
    hc_line("rx", nr, "bytes");
    /* alarms from the last periodic sample */
    {
        health_sample_t ls;
        if (health_last(&ls) == 0) {
            strcpy(msg, "  alarms: ");
            if (ls.alarms == 0) strcat(msg, "none");
            else {
                if (ls.alarms & HEALTH_ALARM_MEM)   strcat(msg, "MEMORY>90% ");
                if (ls.alarms & HEALTH_ALARM_TASKS) strcat(msg, "TASKS>64 ");
                if (ls.alarms & HEALTH_ALARM_CPU)   strcat(msg, "CPU-LOCKED ");
            }
            strcat(msg, "\n");
            screen_console_puts(msg);
        }
    }
    return 0;
}

/* WP-12 spec lists `health watch` and `health export` as their own
 * command-table entries (7 new WP-12 commands in total).  They are
 * registered verbatim (name[32] holds them fine): a quoted input
 * `"health watch"` tokenizes to the single word and dispatches the
 * dedicated handler directly; the unquoted `health watch` keeps the
 * `health`-subcommand path (both run the same watch/export core, so
 * the two entry points are behaviourally identical). */
static int health_watch_core(void) {
    screen_console_puts("health watch: refreshing every second (press ENTER to exit)\n");
    extern int screen_console_in_pump(void);
    int stop = 0;
    while (!stop) {
        if (screen_console_in_pump()) break;
        health_print();
        u64 until = core_timer_now_ms() + 1000;
        while (core_timer_now_ms() < until) {
            if (screen_console_in_pump()) { stop = 1; break; }
        }
    }
    screen_console_puts("-- watch stopped --\n");
    return 0;
}

static int health_export_core(const char *path) {
    static char json[1024];
    int n = health_export(json, (int)sizeof(json));
    if (n <= 0) { screen_console_puts("health: export FAILED\n"); return 1; }
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        screen_console_puts("health: export FAILED (no filesystem mounted?)\n");
        screen_console_puts(json);
        return 1;
    }
    fs_vfs_write(fd, json, n);
    fs_vfs_close(fd);
    char msg[96]; char nb[24];
    strcpy(msg, "health: exported ");
    u64_to_str((u64)n, nb); strcat(msg, nb);
    strcat(msg, " bytes to ");
    strcat(msg, path);
    strcat(msg, "\n");
    screen_console_puts(msg);
    screen_console_puts(json);
    return 0;
}

int shell_cmd_health(const char *args) {
    if (args && strcmp(args, "watch") == 0) {
        return health_watch_core();
    }
    if (args && strncmp(args, "export", 6) == 0) {
        const char *path = "/var/log/health.json";
        static char pbuf[64];
        pbuf[0] = 0;
        if (args[6] == ' ') {
            const char *p = args + 7;
            while (*p == ' ') p++;
            if (*p) {
                strncpy(pbuf, p, sizeof(pbuf) - 1);
                path = pbuf;
            }
        }
        return health_export_core(path);
    }
    if (args && *args) {
        screen_console_puts("usage: health [watch|export [path]]\n");
        return 1;
    }
    return health_print();
}

/* Dedicated table entries (quoted-input dispatch; see the comment
 * block above health_watch_core).  args (if any) is the remainder of
 * the line after the quoted command word. */
int shell_cmd_health_watch(const char *args) {
    (void)args;
    return health_watch_core();
}

int shell_cmd_health_export(const char *args) {
    const char *path = "/var/log/health.json";
    static char pbuf[64];
    pbuf[0] = 0;
    if (args && *args) {
        strncpy(pbuf, args, sizeof(pbuf) - 1);
        pbuf[sizeof(pbuf) - 1] = 0;
        path = pbuf;
    }
    return health_export_core(path);
}


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
OC_SHELL_CMD("health", shell_cmd_health, "system health monitor (health [watch|export])", "watchdog");
OC_SHELL_CMD("health watch", shell_cmd_health_watch, "live health monitor, 1 s refresh (ENTER exits)", "watchdog");
OC_SHELL_CMD("health export", shell_cmd_health_export, "write health JSON to /var/log/health.json", "watchdog");
