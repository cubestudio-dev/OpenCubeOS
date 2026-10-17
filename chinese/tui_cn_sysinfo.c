/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support (fix2 parity rework)
 * File: chinese/tui_cn_sysinfo.c
 * Purpose: System information TUI with Chinese labels - overview /
 *          processes / log / monitor / power / update / time / help,
 *          on the same real kernel data sources as tui/tui_sysinfo.c.
 *
 * fix2 parity: the Chinese pages previously shipped read-only stubs
 * where the English pages had real flows (update: hint at shell
 * commands instead of the in-TUI OTA flow; time: no preferences form;
 * monitor: manual per-keypress sampling; processes: no priority
 * column, no scroll, stale list after kill, no tid>1 guard; overview:
 * no memory/block rows). This file now mirrors the English flows
 * one-for-one; all page text stays Chinese.
 */
#include "tui_chinese.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/lib/lib_config.h"
#include "../kernel/core/core_sched.h"
#include "../kernel/core/core_timer.h"
#include "../kernel/core/core_rtc.h"
#include "../kernel/core/core_power.h"
#include "../kernel/ota/ota_ab.h"
#include "../kernel/ota/ota_update.h"
#include "../drivers/block/driver_block_blk.h"
#include "../mem/mem_pmm.h"
/* boot-log ring (kernel/lib/lib_log.c) */
extern int lib_log_ring_count(void);
extern int lib_log_ring_get(int idx, char *out, int cap);

static void cn_fmt_uptime(char *out, int cap) {
    /* core_timer_now_ms() counts from boot: the uptime is the counter
     * itself.  The previous now-minus-session-start version always
     * reported ~0 because cn_boot_ms was re-taken on every app entry. */
    u64 ms = core_timer_now_ms();
    u64 secs = ms / 1000;
    u64 h = secs / 3600;
    u64 m = (secs % 3600) / 60;
    u64 s = secs % 60;
    char a[8], b[8], c[8];
    u64_to_str(h, a);
    u64_to_str(m, b);
    u64_to_str(s, c);
    int n = 0;
    for (int i = 0; a[i]; i++) out[n++] = a[i];
    out[n++] = ':';
    for (int i = 0; b[i]; i++) out[n++] = b[i];
    out[n++] = ':';
    for (int i = 0; c[i]; i++) out[n++] = c[i];
    out[n] = 0;
    (void)cap;
}

/* ---- shared helpers -------------------------------------------------- */

/* fix2: full-screen windows sized from the real terminal (the fixed
 * 8x60 / 10x36 boxes refused to open at all on narrow terminals). */
static tui_window_t *cn_full_window(const char *title) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (win) tui_window_title(win, title);
    return win;
}

static int cn_read_int_cfg(const char *key, int dflt) {
    char val[16];
    if (lib_config_read(key, val, sizeof(val)) != 0) return dflt;
    int sign = 1, i = 0, v = 0;
    if (val[0] == '-') { sign = -1; i = 1; }
    else if (val[0] == '+') i = 1;
    for (; val[i] >= '0' && val[i] <= '9'; i++) v = v * 10 + (val[i] - '0');
    return sign * v;
}

/* ---- pages ----------------------------------------------------------- */

static void cn_page_overview(tui_window_t *win) {
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "系统概览  [q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    char line[100];
    int p;
    const char *v = "版本: Open Cube OS " OC_RELEASE_VERSION
                     " build ";
    p = 0;
    for (int k = 0; v[k] && p < 90; k++) line[p++] = v[k];
    char bnum[12];
    u64_to_str((u64)OC_KERNEL_BUILD, bnum);
    for (int k = 0; bnum[k] && p < 96; k++) line[p++] = bnum[k];
    line[p] = 0;
    tui_print_at(2, 1, line);
    char up[24];
    cn_fmt_uptime(up, sizeof(up));
    p = 0;
    const char *ul = "运行时间: ";
    for (int k = 0; ul[k]; k++) line[p++] = ul[k];
    for (int k = 0; up[k]; k++) line[p++] = up[k];
    line[p] = 0;
    tui_print_at(3, 1, line);
    core_sched_task_info_t ti[32];
    int nt = core_sched_task_info_get(ti, 32);
    (void)ti;
    char num[16];
    u64_to_str((u64)(nt < 0 ? 0 : nt), num);
    p = 0;
    const char *pl = "任务数: ";
    for (int k = 0; pl[k]; k++) line[p++] = pl[k];
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    line[p] = 0;
    tui_print_at(4, 1, line);

    /* fix2 parity: memory + block-device rows (the English overview had
     * them; the Chinese overview shipped without). */
    mem_pmm_stats_t st;
    mem_pmm_get_stats(&st);
    u64 used_pct = st.total_pages ? (st.used_pages * 100) / st.total_pages : 0;
    p = 0;
    const char *ml = "内存: ";
    for (int k = 0; ml[k]; k++) line[p++] = ml[k];
    u64_to_str(st.used_pages, num);
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    const char *ms = " / ";
    for (int k = 0; ms[k]; k++) line[p++] = ms[k];
    u64_to_str(st.total_pages, num);
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    const char *pe = " 页（";
    for (int k = 0; pe[k]; k++) line[p++] = pe[k];
    u64_to_str(used_pct, num);
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    const char *pu = "% 已用）";
    for (int k = 0; pu[k]; k++) line[p++] = pu[k];
    line[p] = 0;
    tui_print_at(5, 1, line);

    int blocks = 0;
    for (int i = 0; i < 8; i++)
        if (driver_block_get_device(i)) blocks++;
    p = 0;
    const char *bl = "块设备: ";
    for (int k = 0; bl[k]; k++) line[p++] = bl[k];
    u64_to_str((u64)blocks, num);
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    line[p] = 0;
    tui_print_at(6, 1, line);
    tui_window_refresh(win);
}

static int cn_page_processes(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "进程管理（中文）");
    int sel = 0, top = 0;
    for (;;) {
        core_sched_task_info_t ti[32];
        int nt = core_sched_task_info_get(ti, 32);
        if (nt < 0) nt = 0;
        /* fix2: the list is re-queried EVERY loop iteration - after a
         * kill the row really disappears instead of staying stale, and
         * the selection clamps to the live count. */
        if (sel >= nt) sel = nt - 1;
        if (sel < 0) sel = 0;
        int max = rows - 5;
        if (max < 1) max = 1;
        if (sel < top) top = sel;
        if (sel >= top + max) top = sel - max + 1;
        if (top > nt - max) top = (nt > max) ? nt - max : 0;
        if (top < 0) top = 0;

        tui_window_clear(win);
        tui_window_focus_set(win);
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                     TUI_ATTR_BOLD);
        tui_print_at(0, 1, "进程列表  [上下选择  k 终止  q 返回]");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
        tui_print_at(1, 1, "TID  名称            状态  优先级");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        char num[8];
        int shown = (nt - top < max) ? (nt - top) : max;
        for (int i = 0; i < shown; i++) {
            int idx = top + i;
            tui_attr_t save = win->attr;
            if (idx == sel) {
                win->attr = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_TEAL) |
                            TUI_ATTR_BOLD;
            }
            char row[100];
            int p = 0;
            u64_to_str((u64)ti[idx].tid, num);
            for (int k = 0; num[k]; k++) row[p++] = num[k];
            const char *pad = "    ";
            for (int k = 0; pad[k]; k++) row[p++] = pad[k];
            const char *name = ti[idx].name;
            for (int k = 0; name && name[k] && p < 20; k++) row[p++] = name[k];
            while (p < 20) row[p++] = ' ';
            static const char *states[] = { "就绪", "运行", "睡眠", "退出" };
            int st = ti[idx].state;
            if (st < 0 || st > 3) st = 0;
            const char *s = states[st];
            for (int k = 0; s[k]; k++) row[p++] = s[k];
            while (p < 27) row[p++] = ' ';
            /* fix2: the header always promised 优先级 - the rows never
             * printed it. */
            u64_to_str((u64)ti[idx].priority, num);
            for (int k = 0; num[k]; k++) row[p++] = num[k];
            row[p] = 0;
            tui_print_at(i + 2, 1, row);
            win->attr = save;
        }
        tui_window_refresh(win);
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
        else if (k == TUI_KEY_UP && sel > 0) sel--;
        else if (k == TUI_KEY_DOWN && sel + 1 < nt) sel++;
        else if (k == 'k') {
            /* fix2: tid>1 guard (the English page refuses tid<=1; the
             * Chinese page let you kill the scheduler) + honest status */
            if (ti[sel].tid <= 1) {
                tui_status_line("拒绝: 不能终止 tid<=1（内核）");
            } else {
                core_sched_task_exited(ti[sel].tid);
                tui_status_line("已请求终止该任务");
            }
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

static int cn_page_log(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "启动日志（中文界面，日志内容为系统英文记录）");
    int total = lib_log_ring_count();
    int view = total - (rows - 2);
    if (view < 0) view = 0;
    for (;;) {
        tui_window_clear(win);
        tui_window_focus_set(win);
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                     TUI_ATTR_BOLD);
        tui_print_at(0, 1, "启动日志  [上下滚动  q 返回]");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        char line[96];
        for (int r = 1; r < rows - 1; r++) {
            int idx = view + r - 1;
            if (idx >= total) break;
            if (lib_log_ring_get(idx, line, sizeof(line)) == 0) {
                tui_print_at(r, 1, line);
            }
        }
        tui_window_refresh(win);
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC || k == TUI_KEY_F(10)) break;
        else if (k == TUI_KEY_UP && view > 0) view--;
        else if (k == TUI_KEY_DOWN && view + (rows - 2) < total) view++;
    }
    tui_window_free(win);
    tui_update_panels();
    return total;
}

/* fix2 parity: automatic 500 ms sampling (the English monitor looped on
 * tui_getch_timeout(500); the Chinese page demanded Space per sample). */
static int cn_page_monitor(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "监控（中文） [自动采样 Esc 退出]");
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "监控  [每 500ms 自动采样  Esc 退出]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    int sample = 0;
    char line[100];
    for (;;) {
        int k = tui_getch_timeout(500);
        if (k == 'q' || k == TUI_KEY_ESC || k == TUI_KEY_F(10)) break;
        core_sched_task_info_t ti[32];
        int nt = core_sched_task_info_get(ti, 32);
        char num[16];
        int p = 0;
        const char *pre = "采样 ";
        for (int c = 0; pre[c]; c++) line[p++] = pre[c];
        char idx[8];
        u64_to_str((u64)(++sample), idx);
        for (int c = 0; idx[c]; c++) line[p++] = idx[c];
        const char *mid = ": 任务数 ";
        for (int c = 0; mid[c]; c++) line[p++] = mid[c];
        u64_to_str((u64)(nt < 0 ? 0 : nt), num);
        for (int c = 0; num[c]; c++) line[p++] = num[c];
        const char *ms = "  时钟ms ";
        for (int c = 0; ms[c]; c++) line[p++] = ms[c];
        u64_to_str(core_timer_now_ms(), num);
        for (int c = 0; num[c] && p < 94; c++) line[p++] = num[c];
        line[p] = 0;
        int row = (sample - 1) % (rows - 2) + 1;
        tui_print_at(row, 1, line);
        tui_window_refresh(win);
    }
    tui_window_free(win);
    tui_update_panels();
    return sample;
}

static int cn_page_power(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_free(win);
    /* fix3: 休眠 S3 入口隐藏（KNOWN_ISSUES 15.6：部分平台唤醒不可靠） */
    tui_menu_item_t items[3];
    items[0].name = "重启系统";
    items[0].desc = "core_power_reboot（warm reset）";
    items[0].userptr = 0;
    items[1].name = "关机";
    items[1].desc = "halt 并断电（core_power_shutdown）";
    items[1].userptr = 0;
    items[2].name = "取消";
    items[2].desc = "返回上一屏";
    items[2].userptr = 0;
    for (;;) {
        int mw = 36;
        if (mw > cols) mw = cols;
        int mh = 6;
        if (mh > rows) mh = rows;
        tui_window_t *menu = tui_window_new(0, 0, mh, mw);
        if (!menu) return -1;
        tui_window_title(menu, "电源（中文）");
        int pick = tui_chinese_menu(menu, items, 3);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0 || pick == 2) return 0;
        if (pick == 0) { core_power_reboot(); return 1; }
        if (pick == 1) { core_power_shutdown(); return 2; }
    }
}

/* fix2 parity: the REAL in-TUI OTA flow (check -> download -> SHA256 ->
 * install -> set-boot), previously a hint at the shell commands. */
static int cn_page_update(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "系统更新（中文）");
    tui_menu_item_t items[4];
    items[0].name = "检查更新";
    items[0].desc = "联系 update_url（checkupdate）";
    items[0].userptr = 0;
    items[1].name = "下载并安装";
    items[1].desc = "取包、校验 SHA256、装入非活动槽";
    items[1].userptr = 0;
    items[2].name = "回滚";
    items[2].desc = "清除 A/B 引导标志（rollback）";
    items[2].userptr = 0;
    items[3].name = "取消";
    items[3].desc = "返回";
    items[3].userptr = 0;
    tui_menu_t *menu = tui_menu_new(win, items, 4);
    tui_menu_post(menu);
    for (;;) {
        int k = tui_getch();
        int d = tui_menu_driver(menu, k);
        if (d == -1) break;
        if (d != 1) continue;
        int sel = tui_menu_current(menu);
        extern int ota_update_check_pkg(ota_update_pkg_info_t *out);
        ota_update_pkg_info_t pkg;
        if (sel == 0) {
            int r = ota_update_check_pkg(&pkg);
            tui_status_line(r == OC_UPDATE_NEW ? "有可用更新（选「下载并安装」）" :
                            r == OC_UPDATE_OK ? "已是最新版本" :
                            "检查失败（真实错误码）");
            /* fix3: 结果保持显示 -- 原代码落入 tui_menu_post(menu)，
             * 全窗重绘立即盖掉底部状态行。 */
            continue;
        }
        if (sel == 1) {
            int r = ota_update_check_pkg(&pkg);
            if (r != OC_UPDATE_NEW) {
                tui_status_line(r == OC_UPDATE_OK ? "已是最新版本" :
                                "检查失败（真实错误码）");
                continue;   /* fix3 */
            }
            if (!pkg.have_package || !pkg.package_url[0]) {
                tui_status_line("清单缺少包字段（拒绝下载）");
                continue;   /* fix3 */
            }
            tui_status_line("下载中 ...");
            int dr = ota_update_download(pkg.package_url,
                                         "/data/update-pkg.tar.gz");
            if (dr != 0) {
                tui_status_line("下载失败（真实错误码）");
                continue;   /* fix3 */
            }
            tui_status_line("校验 SHA256 ...");
            int vr = ota_update_verify("/data/update-pkg.tar.gz",
                                       pkg.package_sha256);
            if (vr != 0) {
                tui_status_line("校验失败 - 已丢弃该包");
                continue;   /* fix3 */
            }
            int slot = ota_ab_current_slot();
            const char *target = (slot == 1) ? "A" : "B";
            int ir = ota_update_install("/data/update-pkg.tar.gz", target);
            if (ir != 0) {
                tui_status_line("安装失败（真实错误码）");
                continue;   /* fix3 */
            }
            int br = ota_update_set_boot(target);
            if (br == 0)
                tui_status_line("已写入槽位：重启后切换生效（按任意键返回）");
            else
                tui_status_line("已安装但 set-boot 失败");
            tui_getch();    /* fix3: 停留显示结果 */
            break;   /* A/B flags changed: leave the page honestly */
        }
        if (sel == 2) {
            tui_status_line(ota_update_rollback() == 0 ?
                            "回滚标志已清除" : "回滚失败");
            continue;       /* fix3 */
        }
        break;
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* fix2 parity: real preferences form (tui.clock_24h / tui.tz_offset,
 * consumed by both overviews since fix2) + RTC display honoring them. */
static int cn_page_time(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "时间（中文）");
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "系统时间  [回车保存偏好  Esc 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    tui_window_refresh(win);

    static tui_field_t fields[2];
    memset(fields, 0, sizeof(fields));
    fields[0].label = "24 小时制 (1/0):";
    fields[0].y = 2; fields[0].x = 22; fields[0].width = 4; fields[0].editable = 1;
    fields[1].label = "时区偏移(小时) :";
    fields[1].y = 4; fields[1].x = 22; fields[1].width = 4; fields[1].editable = 1;
    /* tui_chinese_form drives its own loop and edits fields[i].buffer
     * in place (0 = submit, -1 = cancel). */
    int h24 = cn_read_int_cfg("tui.clock_24h", 1);
    int tzo = cn_read_int_cfg("tui.tz_offset", 0);
    char vpre[2][8];
    u64_to_str((u64)(h24 ? 1 : 0), vpre[0]);
    u64_to_str((u64)(tzo < 0 ? -tzo : tzo), vpre[1]);
    strcpy(fields[0].buffer, vpre[0]);
    fields[0].len = (int)strlen(fields[0].buffer);
    fields[0].cursor = fields[0].len;
    if (tzo < 0) {
        strcpy(fields[1].buffer, "-");
        strcat(fields[1].buffer, vpre[1]);
    } else {
        strcpy(fields[1].buffer, vpre[1]);
    }
    fields[1].len = (int)strlen(fields[1].buffer);
    fields[1].cursor = fields[1].len;
    int rc = tui_chinese_form(win, fields, 2);
    if (rc == 0) {
        lib_config_write("tui.clock_24h", fields[0].buffer);
        lib_config_write("tui.tz_offset", fields[1].buffer);
        tui_status_line("时间偏好已保存");
    }
    if (rc == 0) {
        /* live clock honoring the saved preferences */
        h24 = cn_read_int_cfg("tui.clock_24h", 1);
        tzo = cn_read_int_cfg("tui.tz_offset", 0);
        core_rtc_time_t t;
        core_rtc_read(&t);
        int hr = t.hour + tzo;
        const char *suffix = "";
        while (hr < 0) hr += 24;
        hr %= 24;
        if (!h24) {
            suffix = (hr >= 12) ? " 下午" : " 上午";
            hr %= 12;
            if (hr == 0) hr = 12;
        }
        char line[100], num[8];
        int p = 0;
        const char *pre = "当前时间: ";
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        if (hr < 10) line[p++] = '0';
        u64_to_str((u64)hr, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ':';
        if (t.minute < 10) line[p++] = '0';
        u64_to_str((u64)t.minute, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ':';
        if (t.second < 10) line[p++] = '0';
        u64_to_str((u64)t.second, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        for (int k = 0; suffix[k]; k++) line[p++] = suffix[k];
        const char *tzs = (tzo >= 0) ? " (UTC+" : " (UTC";
        for (int k = 0; tzs[k]; k++) line[p++] = tzs[k];
        u64_to_str((u64)(tzo < 0 ? -tzo : tzo), num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ')';
        line[p] = 0;
        tui_print_at(6, 1, line);
        tui_window_refresh(win);
        for (;;) {
            int k = tui_getch();
            if (k == 'q' || k == TUI_KEY_ESC || k == TUI_KEY_F(10) ||
                k == TUI_KEY_ENTER) break;
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 1;
}

static void cn_page_help(tui_window_t *win) {
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "帮助  [q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    tui_print_at(2, 1, "系统信息中文界面（WP-10-chinese_support）");
    tui_print_at(3, 1, "概览/进程/日志/监控/电源/更新/时间 均为实时数据");
    tui_print_at(4, 1, "更新页内置完整的 检查/下载/校验/安装/回滚 流程");
    tui_print_at(5, 1, "中文输入: tui-cn 菜单第 4 项「中文输入」(拼音 IME)");
    tui_print_at(6, 1, "  打字即拼音; 数字 1-9 选字; 空格首选; 回车上屏; Esc 退出");
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC || k == TUI_KEY_F(10)) break;
    }
}

int tui_cn_sysinfo_main(void) {
    if (tui_chinese_init() != 0) return -1;
    tui_menu_item_t items[8];
    items[0].name = "系统概览"; items[0].desc = "版本 / 运行时间 / 内存 / 块设备";
    items[1].name = "进程管理"; items[1].desc = "任务列表、滚动与终止";
    items[2].name = "启动日志"; items[2].desc = "内核启动日志浏览";
    items[3].name = "性能监控"; items[3].desc = "每 500ms 自动采样";
    items[4].name = "电源操作"; items[4].desc = "重启 / 关机";
    items[5].name = "系统更新"; items[5].desc = "检查 / 下载 / 校验 / 安装 / 回滚";
    items[6].name = "系统时间"; items[6].desc = "RTC 与显示偏好表单";
    items[7].name = "帮助";     items[7].desc = "使用说明";
    for (int i = 0; i < 8; i++) items[i].userptr = 0;

    int pages_opened = 0;
    for (;;) {
        int cols, rows;
        tui_term_size(&cols, &rows);
        int mw = 36; if (mw > cols) mw = cols;
        int mh = 10; if (mh > rows) mh = rows;
        tui_window_t *menu = tui_window_new(0, 0, mh, mw);
        if (!menu) return -1;
        tui_window_title(menu, "系统信息（中文）");
        int pick = tui_chinese_menu(menu, items, 8);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0) break;
        pages_opened++;
        switch (pick) {
        case 0: {
            tui_window_t *w = cn_full_window("概览（中文）");
            if (w) { cn_page_overview(w);
                     for (;;) { int k = tui_getch();
                                if (k == 'q' || k == TUI_KEY_ESC ||
                                    k == TUI_KEY_F(10)) break; }
                     tui_window_free(w); tui_update_panels(); }
            break;
        }
        case 1: cn_page_processes(); break;
        case 2: cn_page_log(); break;
        case 3: cn_page_monitor(); break;
        case 4: cn_page_power(); break;
        case 5: cn_page_update(); break;
        case 6: cn_page_time(); break;
        case 7: {
            tui_window_t *w = cn_full_window("帮助（中文）");
            if (w) { cn_page_help(w);
                     tui_window_free(w); tui_update_panels(); }
            break;
        }
        }
    }
    return pages_opened;
}
