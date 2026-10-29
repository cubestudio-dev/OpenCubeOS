/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/health ring-3 surface.
 *
 * The v2.2.0 L0 command `health` (recovery/shell_cmds_health.c) migrates
 * to L1: the service renders the report text into the caller's buffer
 * (ring 3 owns presentation), except WATCH which keeps the v2.2.0
 * interactive 1 s refresh loop with direct console output (a multi-
 * screen live view has no one-shot out-buffer semantics).  Logic and
 * honest wording are byte-for-byte the v2.2.0 bodies. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "core_health.h"
#include "screen_console.h"
#include "screen_console_in.h"
#include "lib_string.h"
#include "lib_klog.h"
#include "fs_vfs.h"
#include "core_timer.h"

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

/* v2.2.0 hc_line(): "  <label>: <value> <unit>" */
static void hc_line(const char *label, u64 v, const char *unit) {
    put("    ");
    put(label);
    put(": ");
    put_u64(v);
    if (unit && *unit) { put(" "); put(unit); }
    put("\n");
}

/* v2.2.0 health_print(): fresh sample, console-side wording kept. */
static int health_report_text(void) {
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

    put("system health:\n");
    put("  cpu:\n");
    hc_line("busy", cpu, "% (1 s window)");
    put("  memory:\n");
    hc_line("total", mt, "KiB");
    hc_line("used", mu, "KiB");
    hc_line("free", mf, "KiB");
    put("  tasks:\n");
    hc_line("total", tt, "");
    hc_line("running", tr, "");
    hc_line("ready", trdy, "");
    hc_line("blocked", tb, "");
    put("  disk:\n");
    hc_line("read", dr, "bytes");
    hc_line("written", dw, "bytes");
    put("  network:\n");
    hc_line("tx", nt, "bytes");
    hc_line("rx", nr, "bytes");
    /* alarms from the last periodic sample */
    {
        health_sample_t ls;
        if (health_last(&ls) == 0) {
            put("  alarms: ");
            if (ls.alarms == 0) put("none");
            else {
                if (ls.alarms & HEALTH_ALARM_MEM)   put("MEMORY>90% ");
                if (ls.alarms & HEALTH_ALARM_TASKS) put("TASKS>64 ");
                if (ls.alarms & HEALTH_ALARM_CPU)   put("CPU-LOCKED ");
            }
            put("\n");
        }
    }
    return 0;
}

/* v2.2.0 health_export_core(): write the JSON via the fs service. */
static int health_export_core(const char *path) {
    static char json[1024];
    int n = health_export(json, (int)sizeof(json));
    if (n <= 0) { put("health: export FAILED\n"); return 1; }
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        put("health: export FAILED (no filesystem mounted?)\n");
        put(json);
        return 1;
    }
    fs_vfs_write(fd, json, n);
    fs_vfs_close(fd);
    put("health: exported ");
    put_u64((u64)n);
    put(" bytes to ");
    put(path);
    put("\n");
    put(json);
    return 0;
}

/* v2.2.0 health_watch_core(): interactive, direct console output. */
static int health_watch_core(void) {
    screen_console_puts("health watch: refreshing every second (press ENTER to exit)\n");
    extern int screen_console_in_pump(void);
    int stop = 0;
    while (!stop) {
        if (screen_console_in_pump()) break;
        /* render one report straight to the console */
        {
            static char buf[1024];
            char *o = buf; int left = (int)sizeof(buf);
            char *saved_out = s_out; int saved_left = s_left;
            s_out = o; s_left = left;
            health_report_text();
            *s_out = 0;
            buf[sizeof(buf) - 1] = 0;
            screen_console_puts(buf);
            s_out = saved_out; s_left = saved_left;
        }
        u64 until = core_timer_now_ms() + 1000;
        while (core_timer_now_ms() < until) {
            if (screen_console_in_pump()) { stop = 1; break; }
        }
    }
    screen_console_puts("-- watch stopped --\n");
    return 0;
}

static int health_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
    case L1SVC_HEALTH_SNAPSHOT:
        return health_report_text();
    case L1SVC_HEALTH_EXPORT: {
        const char *path = "/var/log/health.json";
        static char pbuf[64];
        pbuf[0] = 0;
        if (a[0]) {
            strncpy(pbuf, a, sizeof(pbuf) - 1);
            pbuf[sizeof(pbuf) - 1] = 0;
            path = pbuf;
        }
        return health_export_core(path);
    }
    case L1SVC_HEALTH_WATCH:
        return health_watch_core();
    default:
        return -1;
    }
}

/* The category string equals the services/health folder name; the
 * sampling bring-up stays in kmain (boot-phase start, alarms armed). */
OC_SERVICE_DECL_L1("health", "health service (cpu/mem/tasks/disk/net sampling + alarms + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   health_l1_dispatch);
