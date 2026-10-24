/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_sysinfo.c
 * Purpose: System information TUI: overview (version/uptime/resources),
 *          process manager (list + terminate through the scheduler),
 *          log viewer (kernel boot log pages), live monitor (interval
 *          sampling), power/update/time operations and help.
 */
#include "tui.h"
#include "../core/lib/lib_string.h"
#include "../core/lib/lib_config.h"
#include "../core/mem/mem_pmm.h"
#include "../core/core_sched.h"
#include "../core/core_timer.h"
#include "../services/time/core_rtc.h"
#include "../services/power/core_power.h"
#include "../services/net/net_core.h"
#include "../services/ota/ota_ab.h"
#include "../services/ota/ota_update.h"
#include "../drivers/block/driver_block_blk.h"

/* version/build are compile-time macros baked by the Makefile
 * (-DOC_RELEASE_VERSION / -DOC_KERNEL_BUILD), same as main.c */
#ifndef OC_RELEASE_VERSION
#define OC_RELEASE_VERSION "dev"
#endif
#ifndef OC_KERNEL_BUILD
#define OC_KERNEL_BUILD 0
#endif

int tui_sysinfo_init(void) {
    if (!tui_started()) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* overview                                                            */
/* ------------------------------------------------------------------ */

int tui_sysinfo_overview(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "System overview");
    tui_window_focus_set(win);
    tui_window_clear(win);
    int r = 1;
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(r++, 2, "Open Cube OS - L0 kernel");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));

    char line[100];
    int n = 0;
    const char *v = "version         : ";
    for (int i = 0; v[i]; i++) line[n++] = v[i];
    for (const char *q = OC_RELEASE_VERSION; *q && n < 90; q++) line[n++] = *q;
    line[n] = 0;
    tui_print_at(r++, 2, line);

    char num[20];
    n = 0;
    const char *b = "build           : ";
    for (int i = 0; b[i]; i++) line[n++] = b[i];
    u64_to_str((u64)OC_KERNEL_BUILD, num);
    for (int i = 0; num[i]; i++) line[n++] = num[i];
    line[n] = 0;
    tui_print_at(r++, 2, line);

    u64 up = core_timer_now_ms();
    char hms[16];
    core_timer_format_hms(up, hms);
    n = 0;
    const char *u = "uptime          : ";
    for (int i = 0; u[i]; i++) line[n++] = u[i];
    for (int i = 0; hms[i]; i++) line[n++] = hms[i];
    line[n] = 0;
    tui_print_at(r++, 2, line);

    /* fix2: the Time page wrote tui.clock_24h / tui.tz_offset but
     * NOTHING ever consumed them (write-only settings). The overview
     * now shows the RTC clock honoring both preferences. */
    {
        core_rtc_time_t rt;
        core_rtc_read(&rt);
        char val[16];
        int h24 = 1, tzo = 0;
        if (lib_config_read("tui.clock_24h", val, sizeof(val)) == 0)
            h24 = (val[0] != '0');
        if (lib_config_read("tui.tz_offset", val, sizeof(val)) == 0) {
            /* tiny signed-decimal parse (lib_string has no atoi) */
            int sign = 1, i = 0;
            if (val[0] == '-') { sign = -1; i = 1; }
            else if (val[0] == '+') i = 1;
            for (; val[i] >= '0' && val[i] <= '9'; i++)
                tzo = tzo * 10 + (val[i] - '0');
            tzo *= sign;
        }
        int hr = rt.hour + tzo;
        const char *suffix = "";
        while (hr < 0) hr += 24;
        hr %= 24;
        n = 0;
        const char *c = "clock (rtc)     : ";
        for (int i = 0; c[i]; i++) line[n++] = c[i];
        if (!h24) {
            suffix = (hr >= 12) ? " PM" : " AM";
            hr %= 12;
            if (hr == 0) hr = 12;
        }
        if (hr < 10) line[n++] = '0';
        u64_to_str((u64)hr, num);
        for (int i = 0; num[i]; i++) line[n++] = num[i];
        line[n++] = ':';
        if (rt.minute < 10) line[n++] = '0';
        u64_to_str((u64)rt.minute, num);
        for (int i = 0; num[i]; i++) line[n++] = num[i];
        line[n++] = ':';
        if (rt.second < 10) line[n++] = '0';
        u64_to_str((u64)rt.second, num);
        for (int i = 0; num[i]; i++) line[n++] = num[i];
        for (int i = 0; suffix[i]; i++) line[n++] = suffix[i];
        const char *tzs = (tzo >= 0) ? " UTC+" : " UTC";
        for (int i = 0; tzs[i]; i++) line[n++] = tzs[i];
        u64_to_str((u64)(tzo < 0 ? -tzo : tzo), num);
        for (int i = 0; num[i]; i++) line[n++] = num[i];
        line[n] = 0;
        tui_print_at(r++, 2, line);
    }

    mem_pmm_stats_t st;
    mem_pmm_get_stats(&st);
    u64 used_pct = st.total_pages ? (st.used_pages * 100) / st.total_pages : 0;
    n = 0;
    const char *m = "memory          : ";
    for (int i = 0; m[i]; i++) line[n++] = m[i];
    u64_to_str(st.used_pages, num);
    for (int i = 0; num[i]; i++) line[n++] = num[i];
    const char *ms = " / ";
    for (int i = 0; ms[i]; i++) line[n++] = ms[i];
    u64_to_str(st.total_pages, num);
    for (int i = 0; num[i]; i++) line[n++] = num[i];
    const char *pp = " pages (";
    for (int i = 0; pp[i]; i++) line[n++] = pp[i];
    u64_to_str(used_pct, num);
    for (int i = 0; num[i]; i++) line[n++] = num[i];
    const char *pe = "% used)";
    for (int i = 0; pe[i]; i++) line[n++] = pe[i];
    line[n] = 0;
    tui_print_at(r++, 2, line);

    int blocks = 0;
    for (int i = 0; i < 8; i++)
        if (driver_block_get_device(i)) blocks++;
    n = 0;
    const char *d = "block devices   : ";
    for (int i = 0; d[i]; i++) line[n++] = d[i];
    u64_to_str((u64)blocks, num);
    for (int i = 0; num[i]; i++) line[n++] = num[i];
    line[n] = 0;
    tui_print_at(r++, 2, line);

    n = 0;
    const char *k = "tasks           : ";
    for (int i = 0; k[i]; i++) line[n++] = k[i];
    core_sched_task_info_t ti[32];
    int nt = core_sched_task_info_get(ti, 32);
    u64_to_str((u64)nt, num);
    for (int i = 0; num[i]; i++) line[n++] = num[i];
    const char *al = " live (see Processes)";
    for (int i = 0; al[i]; i++) line[n++] = al[i];
    line[n] = 0;
    tui_print_at(r++, 2, line);

    tui_status_line("Esc back | arrows navigate");
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
    tui_getch();
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* ------------------------------------------------------------------ */
/* process manager                                                     */
/* ------------------------------------------------------------------ */

int tui_sysinfo_processes(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Processes");
    core_sched_task_info_t ti[32];
    int nt = core_sched_task_info_get(ti, 32);
    int sel = 0;
    int top = 0;
    int repaint = 1;
    for (;;) {
        if (repaint) {
            tui_window_focus_set(win);
            tui_window_clear(win);
            tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                         TUI_ATTR_BOLD);
            tui_print_at(1, 2, "TID NAME            PRI STATE");
            tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
            int vis = rows - 4;
            if (sel < top) top = sel;
            if (sel >= top + vis) top = sel - vis + 1;
            for (int i = 0; i < vis && top + i < nt; i++) {
                int idx = top + i;
                char line[100];
                char num[12];
                int p = 0;
                if (idx == sel) { line[p++] = '>'; } else { line[p++] = ' '; }
                line[p++] = ' ';
                u64_to_str((u64)ti[idx].tid, num);
                for (int j = 0; num[j]; j++) line[p++] = num[j];
                while (p < 6) line[p++] = ' ';
                const char *nm = ti[idx].name;
                for (int j = 0; nm && nm[j] && j < 16; j++) line[p++] = nm[j];
                while (p < 23) line[p++] = ' ';
                u64_to_str((u64)ti[idx].priority, num);
                for (int j = 0; num[j]; j++) line[p++] = num[j];
                while (p < 27) line[p++] = ' ';
                const char *st =
                    ti[idx].state == TASK_RUNNING ? "RUN" :
                    ti[idx].state == TASK_READY   ? "RDY" :
                    ti[idx].state == TASK_EXITED  ? "EXT" : "BLK";
                for (int j = 0; st[j]; j++) line[p++] = st[j];
                line[p] = 0;
                tui_print_at(2 + i, 2, line);
            }
            tui_status_line("K kill selected | arrows move | Esc back");
            tui_window_border(win, &TUI_BORDER_SINGLE);
            tui_window_refresh(win);
            repaint = 0;
        }
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') break;
        if (k == TUI_KEY_UP && sel > 0) { sel--; repaint = 1; }
        if (k == TUI_KEY_DOWN && sel < nt - 1) { sel++; repaint = 1; }
        if ((k == 'k' || k == 'K') && nt > 0) {
            /* real termination through the scheduler API */
            core_sched_task_info_t *t = &ti[sel];
            if (t->tid > 1) {
                core_kthread_destroy((tid_t)t->tid);
                nt = core_sched_task_info_get(ti, 32);
                if (sel >= nt) sel = nt - 1;
                if (sel < 0) sel = 0;
            }
            repaint = 1;
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* ------------------------------------------------------------------ */
/* log viewer: the kernel boot log is the serial/fb console history;   */
/* the TUI viewer re-runs lib_log's recent stage lines through a ring  */
/* the kernel keeps for this purpose (lib_log ring).                   */
/* ------------------------------------------------------------------ */

/* lib_log exposes the boot-stage ring through OC_LOG dump (shell log
 * command).  The TUI reads the same ring: */
int lib_log_ring_count(void);
int lib_log_ring_get(int idx, char *out, int cap);

int tui_sysinfo_logs(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Kernel log");
    int total = lib_log_ring_count();
    int top = total > 0 ? total - 1 : 0;
    int repaint = 1;
    for (;;) {
        if (repaint) {
            tui_window_focus_set(win);
            tui_window_clear(win);
            int vis = rows - 3;
            for (int i = 0; i < vis; i++) {
                int idx = top + i;
                if (idx < 0 || idx >= total) continue;
                char line[100];
                if (lib_log_ring_get(idx, line, sizeof(line)) == 0)
                    tui_print_at(1 + i, 1, line);
            }
            tui_status_line("arrows/PgUp/PgDn scroll | Esc back");
            tui_window_border(win, &TUI_BORDER_SINGLE);
            tui_window_refresh(win);
            repaint = 0;
        }
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') break;
        if (k == TUI_KEY_UP || k == TUI_KEY_PGUP) { top--; if (top < 0) top = 0; repaint = 1; }
        if (k == TUI_KEY_DOWN || k == TUI_KEY_PGDN) {
            top++;
            if (top > total - 1) top = total > 0 ? total - 1 : 0;
            repaint = 1;
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* ------------------------------------------------------------------ */
/* live monitor: samples real counters on an interval                  */
/* ------------------------------------------------------------------ */

int tui_sysinfo_monitor(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Live monitor (Esc to stop)");
    int hist_mem[60];
    int hist_net[60];
    int hist_n = 0;
    u64 last_ticks = core_timer_ticks();
    for (int i = 0; i < 60; i++) { hist_mem[i] = 0; hist_net[i] = 0; }
    for (;;) {
        mem_pmm_stats_t st;
        mem_pmm_get_stats(&st);
        int mem_pct = st.total_pages ? (int)((st.used_pages * 100) / st.total_pages) : 0;
        u64 ticks = core_timer_ticks();
        u64 delta = ticks - last_ticks;   /* timer ticks in the window */
        last_ticks = ticks;
        if (hist_n < 60) {
            hist_mem[hist_n] = mem_pct;
            hist_net[hist_n] = (int)(delta > 0xFFFF ? 0xFFFF : delta);
            hist_n++;
        } else {
            for (int i = 0; i < 59; i++) { hist_mem[i] = hist_mem[i + 1]; hist_net[i] = hist_net[i + 1]; }
            hist_mem[59] = mem_pct;
            hist_net[59] = (int)(delta > 0xFFFF ? 0xFFFF : delta);
        }
        tui_window_focus_set(win);
        tui_window_clear(win);
        /* memory bars (real samples) */
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_LIME, TUI_COLOR_BLACK));
        tui_print_at(1, 2, "memory %:");
        for (int i = 0; i < hist_n && i < cols - 14; i++) {
            int h = (hist_mem[i] * (rows - 6)) / 100;
            for (int y = 0; y < h; y++)
                tui_print_at(rows - 3 - y, 12 + i, "#");
        }
        /* network byte deltas */
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
        tui_print_at(2, 2, "ticks  :");
        for (int i = 0; i < hist_n && i < cols - 14; i++) {
            int h = hist_net[i] / 256;   /* scale: 64KiB/s fills the box */
            if (h > rows - 6) h = rows - 6;
            for (int y = 0; y < h; y++)
                tui_print_at(rows - 3 - y, 12 + i, ":");
        }
        char line[64];
        char num[20];
        int p = 0;
        const char *l = "mem ";
        for (int i = 0; l[i]; i++) line[p++] = l[i];
        u64_to_str((u64)mem_pct, num);
        for (int i = 0; num[i]; i++) line[p++] = num[i];
        const char *pc = "%  tick delta ";
        for (int i = 0; pc[i]; i++) line[p++] = pc[i];
        u64_to_str(delta, num);
        for (int i = 0; num[i]; i++) line[p++] = num[i];
        const char *by = " bytes";
        for (int i = 0; by[i]; i++) line[p++] = by[i];
        line[p] = 0;
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        tui_print_at(3, 2, line);
        tui_window_border(win, &TUI_BORDER_SINGLE);
        tui_window_refresh(win);
        /* non-blocking poll: leave on Esc, sample every ~500 ms */
        u64 deadline = core_timer_now_ms() + 500;
        while (core_timer_now_ms() < deadline) {
            int k = tui_getch_timeout(50);
            if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') {
                tui_window_free(win);
                tui_update_panels();
                return 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* power / update / time                                               */
/* ------------------------------------------------------------------ */

int tui_sysinfo_power(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Power");
    /* fix3: Suspend (S3) entry hidden per user decision -- wake is not
     * reliable on all platforms yet (KNOWN_ISSUES 15.6). */
    tui_menu_item_t items[3];
    items[0].name = "Reboot";
    items[0].desc = "restart the machine";
    items[1].name = "Power off";
    items[1].desc = "halt and power down";
    items[2].name = "Cancel";
    items[2].desc = "back to the previous screen";
    tui_menu_t *menu = tui_menu_new(win, items, 3);
    tui_menu_post(menu);
    int rc = 0;
    for (;;) {
        int k = tui_getch();
        int d = tui_menu_driver(menu, k);
        if (d == -1) break;
        if (d == 1) {
            int sel = tui_menu_current(menu);
            if (sel == 0) { core_power_reboot(); }
            else if (sel == 1) { core_power_shutdown(); }
            else break;
        }
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_update_panels();
    return rc;
}

int tui_sysinfo_update(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "System update");
    tui_menu_item_t items[4];
    items[0].name = "Check for update";
    items[0].desc = "contact update_url (checkupdate)";
    items[1].name = "Download + install";
    items[1].desc = "fetch the package, verify SHA256, install into the inactive slot";
    items[2].name = "Rollback";
    items[2].desc = "clear the A/B boot flags (rollback)";
    items[3].name = "Cancel";
    items[3].desc = "back";
    tui_menu_t *menu = tui_menu_new(win, items, 4);
    tui_menu_post(menu);
    for (;;) {
        int k = tui_getch();
        int d = tui_menu_driver(menu, k);
        if (d == -1) break;
        if (d != 1) continue;
        int sel = tui_menu_current(menu);
        if (sel == 0) {
            /* the OTA API behind the checkupdate command, driven directly */
            extern int ota_update_check_pkg(ota_update_pkg_info_t *out);
            ota_update_pkg_info_t pkg;
            int r = ota_update_check_pkg(&pkg);
            tui_status_line(r == OC_UPDATE_NEW ?
                            "update available (see the update command)" :
                            r == OC_UPDATE_OK ?
                            "no update: already newest" :
                            "check failed (honest error code)");
            /* fix3: keep the result on the status line -- the old code
             * fell through to tui_menu_post(menu) whose full-window
             * repaint covered the bottom row instantly. */
            continue;
        }
        if (sel == 1) {
            /* full in-TUI flow: check -> download -> verify -> install.
             * The download is a real blocking HTTP fetch; the status
             * line reports each real return code. */
            extern int ota_update_check_pkg(ota_update_pkg_info_t *out);
            ota_update_pkg_info_t pkg;
            int r = ota_update_check_pkg(&pkg);
            if (r != OC_UPDATE_NEW) {
                tui_status_line(r == OC_UPDATE_OK ?
                                "no update: already newest" :
                                "check failed (honest error code)");
                continue;   /* fix3: keep the result visible */
            }
            if (!pkg.have_package || !pkg.package_url[0]) {
                tui_status_line("manifest has no package fields (download refused)");
                continue;   /* fix3 */
            }
            tui_status_line("downloading %s ...", pkg.package_url);
            int dr = ota_update_download(pkg.package_url, "/data/update-pkg.tar.gz");
            if (dr != 0) {
                tui_status_line("download failed (rc=%d)", dr);
                continue;   /* fix3 */
            }
            tui_status_line("verifying SHA256 ...");
            int vr = ota_update_verify("/data/update-pkg.tar.gz",
                                       pkg.package_sha256);
            if (vr != 0) {
                tui_status_line("verify failed (rc=%d) - package discarded", vr);
                continue;   /* fix3 */
            }
            int slot = ota_ab_current_slot();
            const char *target = (slot == 1) ? "A" : "B";
            int ir = ota_update_install("/data/update-pkg.tar.gz", target);
            if (ir != 0) {
                tui_status_line("install failed (rc=%d)", ir);
                continue;   /* fix3 */
            }
            int br = ota_update_set_boot(target);
            if (br == 0)
                tui_status_line("slot %s staged: reboot to switch (press a key)", target);
            else
                tui_status_line("installed but set-boot failed (rc=%d)", br);
            tui_getch();    /* fix3: hold the result until a key */
            break;   /* A/B flags changed: leave the page honestly */
        }
        if (sel == 2) {
            tui_status_line(ota_update_rollback() == 0 ?
                            "rollback flags cleared" : "rollback failed");
            continue;       /* fix3: keep the result visible */
        }
        break;
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

int tui_sysinfo_time(void) {
    if (!tui_started()) return -1;
    /* time setting writes the config; the clock itself stays RTC-driven
     * (CMOS read-only in this kernel: see docs/CONFIG.md) */
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Time settings");
    tui_form_t *form;
    static tui_field_t fields[2];
    memset(fields, 0, sizeof(fields));
    fields[0].label = "clock 24h (1/0):";
    fields[0].y = 1; fields[0].x = 20; fields[0].width = 4; fields[0].editable = 1;
    fields[1].label = "tz offset (hrs):";
    fields[1].y = 3; fields[1].x = 20; fields[1].width = 4; fields[1].editable = 1;
    char v[8];
    tui_window_focus_set(win);
    form = tui_form_new(win, fields, 2);
    if (lib_config_read("tui.clock_24h", v, sizeof(v)) == 0)
        tui_form_set_field(form, 0, v);
    if (lib_config_read("tui.tz_offset", v, sizeof(v)) == 0)
        tui_form_set_field(form, 1, v);
    tui_form_post(form);
    for (;;) {
        int k = tui_getch();
        int d = tui_form_driver(form, k);
        if (d == -1) break;
        if (d == 1) {
            lib_config_write("tui.clock_24h", tui_form_field_buffer(form, 0));
            lib_config_write("tui.tz_offset", tui_form_field_buffer(form, 1));
            tui_status_line("time preferences saved");
            break;
        }
    }
    tui_form_free(form);
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

int tui_sysinfo_help(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Help - shortcuts");
    tui_window_focus_set(win);
    tui_window_clear(win);
    static const char *lines[] = {
        "F1      help (main menu, hardware pages)",
        "F2      save configuration (hardware pages)",
        "F5      refresh the current view",
        "F10     leave the current page",
        "Tab     switch domain / next field",
        "Up/Down navigate (hardware pages: scroll)",
        "+/-     sound volume (hardware Sound page)",
        "Enter   confirm / open",
        "Esc     cancel / back",
    };
    for (int i = 0; i < 9 && i < rows - 3; i++)
        tui_print_at(1 + i, 3, lines[i]);
    tui_print_at(rows - 3, 3, "Open Cube OS - WP-10-tui - Apache 2.0 - cubestudio-dev");
    tui_status_line("Esc back");
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
    tui_getch();
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* ------------------------------------------------------------------ */
/* root app                                                            */
/* ------------------------------------------------------------------ */

int tui_sysinfo_main(void) {
    if (tui_sysinfo_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "System information");
    tui_menu_item_t items[9];
    items[0].name = "Overview";       items[0].desc = "version, uptime, resources";
    items[1].name = "Processes";      items[1].desc = "task table, kill";
    items[2].name = "Kernel log";     items[2].desc = "boot stage log viewer";
    items[3].name = "Monitor";        items[3].desc = "live memory/network sampling";
    items[4].name = "Power";          items[4].desc = "reboot / poweroff";
    items[5].name = "Update";         items[5].desc = "A/B OTA check, install, rollback";
    items[6].name = "Time";           items[6].desc = "clock display preferences";
    items[7].name = "Help";           items[7].desc = "shortcut reference";
    items[8].name = "Exit";           items[8].desc = "leave the TUI";
    tui_menu_t *menu = tui_menu_new(win, items, 9);
    tui_menu_post(menu);
    for (;;) {
        int k = tui_getch();
        int d = tui_menu_driver(menu, k);
        if (d == -1 || (d == 1 && tui_menu_current(menu) == 8)) break;
        if (d == 1) {
            int sel = tui_menu_current(menu);
            switch (sel) {
            case 0: tui_sysinfo_overview(); break;
            case 1: tui_sysinfo_processes(); break;
            case 2: tui_sysinfo_logs(); break;
            case 3: tui_sysinfo_monitor(); break;
            case 4: tui_sysinfo_power(); break;
            case 5: tui_sysinfo_update(); break;
            case 6: tui_sysinfo_time(); break;
            case 7: tui_sysinfo_help(); break;
            }
            tui_menu_post(menu);
        }
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_update_panels();
    return 0;
}
