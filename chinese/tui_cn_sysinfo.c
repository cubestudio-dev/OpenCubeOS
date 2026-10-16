/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/tui_cn_sysinfo.c
 * Purpose: System information TUI with Chinese labels - overview /
 *          processes / log / monitor / power / update / time / help,
 *          on the same real kernel data sources as tui/tui_sysinfo.c.
 */
#include "tui_chinese.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/core/core_sched.h"
#include "../kernel/core/core_timer.h"
#include "../kernel/core/core_rtc.h"
#include "../kernel/core/core_power.h"
#include "../kernel/ota/ota_ab.h"
#include "../kernel/ota/ota_update.h"
/* boot-log ring (kernel/lib/lib_log.c) */
extern int lib_log_ring_count(void);
extern int lib_log_ring_get(int idx, char *out, int cap);

static u64 cn_boot_ms;

static void cn_fmt_uptime(char *out, int cap) {
    u64 ms = core_timer_now_ms() - cn_boot_ms;
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
    char num[8];
    u64_to_str((u64)(nt < 0 ? 0 : nt), num);
    p = 0;
    const char *pl = "任务数: ";
    for (int k = 0; pl[k]; k++) line[p++] = pl[k];
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    line[p] = 0;
    tui_print_at(4, 1, line);
    tui_window_refresh(win);
}

static int cn_page_processes(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "进程管理（中文）");
    core_sched_task_info_t ti[32];
    int nt = core_sched_task_info_get(ti, 32);
    int sel = 0;
    for (;;) {
        tui_window_clear(win);
        tui_window_focus_set(win);
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                     TUI_ATTR_BOLD);
        tui_print_at(0, 1, "进程列表  [上下选择  k 终止  q 返回]");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
        tui_print_at(1, 1, "TID  名称            状态  优先级");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        int max = rows - 4;
        if (max > nt) max = nt < 0 ? 0 : nt;
        char num[8];
        for (int i = 0; i < max; i++) {
            tui_attr_t save = win->attr;
            if (i == sel) {
                win->attr = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_TEAL) |
                            TUI_ATTR_BOLD;
            }
            char row[100];
            int p = 0;
            u64_to_str((u64)ti[i].tid, num);
            for (int k = 0; num[k]; k++) row[p++] = num[k];
            const char *pad = "    ";
            for (int k = 0; pad[k]; k++) row[p++] = pad[k];
            const char *name = ti[i].name;
            for (int k = 0; name && name[k] && p < 20; k++) row[p++] = name[k];
            while (p < 20) row[p++] = ' ';
            static const char *states[] = { "就绪", "运行", "睡眠", "僵尸" };
            int st = ti[i].state;
            if (st < 0 || st > 3) st = 0;
            const char *s = states[st];
            for (int k = 0; s[k]; k++) row[p++] = s[k];
            row[p] = 0;
            tui_print_at(i + 2, 1, row);
            win->attr = save;
        }
        tui_window_refresh(win);
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
        else if (k == TUI_KEY_UP && sel > 0) sel--;
        else if (k == TUI_KEY_DOWN && sel + 1 < max) sel++;
        else if (k == 'k') {
            core_sched_task_exited(ti[sel].tid);
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return nt;
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
        if (k == 'q' || k == TUI_KEY_ESC) break;
        else if (k == TUI_KEY_UP && view > 0) view--;
        else if (k == TUI_KEY_DOWN && view + (rows - 2) < total) view++;
    }
    tui_window_free(win);
    tui_update_panels();
    return total;
}

/* monitor: per-keypress sampling (the kernel has no sleep call; the
 * sampler advances on each key so every sample is real). */
static int cn_page_monitor(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "监控（中文） [空格采样 q 退出]");
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "监控  [空格 采样  q 退出]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    int sample = 0;
    char line[100];
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
        if (k != ' ') continue;
        core_sched_task_info_t ti[32];
        int nt = core_sched_task_info_get(ti, 32);
        char num[12];
        int p = 0;
        const char *pre = "采样 ";
        for (int c = 0; pre[c]; c++) line[p++] = pre[c];
        char idx[4];
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
    tui_menu_item_t items[3];
    items[0].name = "重启系统";
    items[0].desc = "core_power_reboot（warm reset）";
    items[0].userptr = 0;
    items[1].name = "关机";
    items[1].desc = "halt 并断电（core_power_shutdown）";
    items[1].userptr = 0;
    items[2].name = "休眠 S3";
    items[2].desc = "core_power_suspend（S3 挂起到内存）";
    items[2].userptr = 0;
    for (;;) {
        tui_window_t *menu = tui_window_new(0, 0, 5, 36);
        if (!menu) return -1;
        tui_window_title(menu, "电源（中文）");
        int pick = tui_chinese_menu(menu, items, 3);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0) return 0;
        if (pick == 0) { core_power_reboot(); return 1; }
        if (pick == 1) { core_power_shutdown(); return 2; }
        if (pick == 2) { core_power_suspend(); return 3; }
    }
}

static int cn_page_update(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "系统更新（中文）");
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "A/B 更新状态  [q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    char line[100];
    int p = 0;
    const char *pre = "当前运行槽: ";
    for (int k = 0; pre[k]; k++) line[p++] = pre[k];
    int slot = ota_ab_current_slot();
    const char *s = slot == 1 ? "B" : "A";
    for (int k = 0; s[k]; k++) line[p++] = s[k];
    line[p] = 0;
    tui_print_at(2, 1, line);
    p = 0;
    const char *ul = "在线更新命令: checkupdate / update / rollback";
    for (int k = 0; ul[k]; k++) line[p++] = ul[k];
    line[p] = 0;
    tui_print_at(3, 1, line);
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
    }
    tui_window_free(win);
    tui_update_panels();
    return slot >= 0 ? 1 : 0;
}

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
    tui_print_at(0, 1, "系统时间  [q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    core_rtc_time_t t;
    char line[100];
    int p = 0;
    core_rtc_read(&t);
    {
        char num[8];
        const char *pre = "RTC 时间: ";
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        u64_to_str((u64)t.year, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = '-';
        if (t.month < 10) line[p++] = '0';
        u64_to_str((u64)t.month, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = '-';
        if (t.day < 10) line[p++] = '0';
        u64_to_str((u64)t.day, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ' ';
        if (t.hour < 10) line[p++] = '0';
        u64_to_str((u64)t.hour, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ':';
        if (t.minute < 10) line[p++] = '0';
        u64_to_str((u64)t.minute, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p++] = ':';
        if (t.second < 10) line[p++] = '0';
        u64_to_str((u64)t.second, num);
        for (int k = 0; num[k]; k++) line[p++] = num[k];
    }
    line[p] = 0;
    tui_print_at(2, 1, line);
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
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
    tui_print_at(4, 1, "q / Esc 返回上级菜单");
    tui_print_at(5, 1, "中文输入: ime 命令（拼音 IME，GB2312 一级字）");
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
    }
}

int tui_cn_sysinfo_main(void) {
    if (tui_chinese_init() != 0) return -1;
    cn_boot_ms = core_timer_now_ms();
    tui_menu_item_t items[8];
    items[0].name = "系统概览"; items[0].desc = "版本 / 运行时间 / 任务数";
    items[1].name = "进程管理"; items[1].desc = "任务列表与终止";
    items[2].name = "启动日志"; items[2].desc = "内核启动日志浏览";
    items[3].name = "性能监控"; items[3].desc = "任务与时钟采样";
    items[4].name = "电源操作"; items[4].desc = "重启 / 关机 / 休眠";
    items[5].name = "系统更新"; items[5].desc = "A/B 槽与在线更新";
    items[6].name = "系统时间"; items[6].desc = "RTC 实时时钟";
    items[7].name = "帮助";     items[7].desc = "使用说明";
    for (int i = 0; i < 8; i++) items[i].userptr = 0;

    int pages_opened = 0;
    int cols, rows;
    tui_term_size(&cols, &rows);
    for (;;) {
        tui_window_t *menu = tui_window_new(0, 0, 10, 36);
        if (!menu) return -1;
        tui_window_title(menu, "系统信息（中文）");
        int pick = tui_chinese_menu(menu, items, 8);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0) break;
        pages_opened++;
        tui_window_t *full = tui_window_new(0, 0, rows, cols);
        if (!full) continue;
        tui_window_free(full);
        switch (pick) {
        case 0: {
            tui_window_t *w = tui_window_new(0, 0, 8, 60);
            if (w) { tui_window_title(w, "概览（中文）");
                     cn_page_overview(w);
                     for (;;) { int k = tui_getch();
                                if (k == 'q' || k == TUI_KEY_ESC) break; }
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
            tui_window_t *w = tui_window_new(0, 0, 8, 60);
            if (w) { tui_window_title(w, "帮助（中文）");
                     cn_page_help(w);
                     tui_window_free(w); tui_update_panels(); }
            break;
        }
        }
    }
    return pages_opened;
}
