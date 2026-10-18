/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_dt_proc.c
 * Purpose: the desktop task manager - live process table (g_procs),
 *          real PMM memory statistics, uptime, and Kill.  F5 refreshes;
 *          Enter opens a per-process detail page; k kills the selected
 *          process through user_process_kill (the same path `kill`
 *          uses).  All data is read from the real subsystems.
 */
#include "tui_desktop.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/core/core_usermode.h"
#include "../kernel/core/core_timer.h"
#include "../kernel/mem/mem_pmm.h"
#include "../drivers/input/driver_input_keyboard.h"

#define PR_MAX_ROWS 12

/* collect the alive processes into a flat view */
typedef struct {
    pid_t pid;
    pid_t ppid;
    tid_t tid;
    u64   brk;
    char  name[32];
} pr_row_t;

static pr_row_t g_rows[PR_MAX_ROWS];
static int      g_row_count = 0;
static int      g_sel = 0;

static void pr_scan(void) {
    g_row_count = 0;
    for (int i = 0; i < MAX_USER_PROCS && g_row_count < PR_MAX_ROWS; i++) {
        if (!g_procs[i].alive) continue;
        g_rows[g_row_count].pid = g_procs[i].pid;
        g_rows[g_row_count].ppid = g_procs[i].ppid;
        g_rows[g_row_count].tid = g_procs[i].tid;
        g_rows[g_row_count].brk = g_procs[i].brk;
        strncpy(g_rows[g_row_count].name, g_procs[i].name, 31);
        g_rows[g_row_count].name[31] = 0;
        g_row_count++;
    }
    if (g_sel >= g_row_count) g_sel = g_row_count ? g_row_count - 1 : 0;
    if (g_sel < 0) g_sel = 0;
}

static void pr_render(void) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
    tui_set_attr(hdr);
    for (int x = 0; x < cols; x++) tui_print_at(0, x, " ");
    tui_print_at(0, 1, tui_tr("Task manager", "任务管理器"));
    tui_set_attr(tui_default_attr());

    /* memory + uptime summary (real PMM stats) */
    mem_pmm_stats_t st;
    mem_pmm_get_stats(&st);
    char line[120], num[24];
    u64 total_kib = st.total_bytes >> 10;
    u64 used_kib = st.used_bytes >> 10;
    u64_to_str(used_kib, num);
    int m = 0;
    strncpy(line, tui_tr(" mem: ", " 内存："), 20);
    m = (int)strlen(line);
    for (int i = 0; num[i]; i++) line[m++] = num[i];
    const char *mid = tui_tr(" KiB / ", " KiB / ");
    for (int i = 0; mid[i]; i++) line[m++] = mid[i];
    u64_to_str(total_kib, num);
    for (int i = 0; num[i]; i++) line[m++] = num[i];
    const char *tail = tui_tr(" KiB   uptime: ", " KiB   运行：");
    for (int i = 0; tail[i]; i++) line[m++] = tail[i];
    u64 secs = core_timer_now_ms() / 1000;
    u64_to_str(secs, num);
    for (int i = 0; num[i]; i++) line[m++] = num[i];
    const char *s2 = tui_tr(" s", " 秒");
    for (int i = 0; s2[i]; i++) line[m++] = s2[i];
    line[m] = 0;
    tui_print_at(1, 1, line);

    /* column header */
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK) | TUI_ATTR_BOLD);
    tui_print_at(2, 1, tui_tr(
        "PID   PPID  TID   BRK(KiB)  NAME",
        "PID   PPID  TID   BRK(KiB)  名称"));
    tui_set_attr(tui_default_attr());

    for (int r = 3; r < rows - 2; r++) {
        tui_set_attr(tui_default_attr());
        for (int x = 0; x < cols; x++) tui_print_at(r, x, " ");
    }
    for (int i = 0; i < g_row_count; i++) {
        int row = 3 + i;
        if (row >= rows - 2) break;
        tui_set_attr(i == g_sel
            ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_SILVER) | TUI_ATTR_BOLD)
            : TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        char rowbuf[96];
        int n = 0;
        char num2[16];
        u64_to_str((u64)g_rows[i].pid, num2);
        for (int k = 0; num2[k]; k++) rowbuf[n++] = num2[k];
        while (n < 6) rowbuf[n++] = ' ';
        u64_to_str((u64)g_rows[i].ppid, num2);
        for (int k = 0; num2[k]; k++) rowbuf[n++] = num2[k];
        while (n < 12) rowbuf[n++] = ' ';
        u64_to_str((u64)g_rows[i].tid, num2);
        for (int k = 0; num2[k]; k++) rowbuf[n++] = num2[k];
        while (n < 18) rowbuf[n++] = ' ';
        u64_to_str(g_rows[i].brk >> 10, num2);
        for (int k = 0; num2[k]; k++) rowbuf[n++] = num2[k];
        while (n < 28) rowbuf[n++] = ' ';
        for (int k = 0; g_rows[i].name[k] && n < 60; k++) rowbuf[n++] = g_rows[i].name[k];
        rowbuf[n] = 0;
        tui_print_at(row, 1, rowbuf);
    }
    if (g_row_count == 0) {
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        tui_print_at(3, 1, tui_tr(
            "  (no user processes alive)",
            "  （当前无存活用户进程）"));
    }

    /* toast + key bar */
    tui_set_attr(tui_default_attr());
    for (int x = 0; x < cols; x++) tui_print_at(rows - 2, x, " ");
    int tok = 1;
    const char *txt = tui_dt_toast_text(&tok);
    if (txt[0]) {
        tui_set_attr(tok
            ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_LIME, TUI_COLOR_BLACK) | TUI_ATTR_BOLD)
            : (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_MAROON) | TUI_ATTR_REVERSE));
        tui_print_at(rows - 2, 1, txt);
    }
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA));
    for (int x = 0; x < cols; x++) tui_print_at(rows - 1, x, " ");
    tui_print_at(rows - 1, 0, tui_tr(
        " Up/Down:Select Enter:Detail k:Kill F5:Refresh Esc:Exit ",
        " 上下:选择 Enter:详情 k:结束进程 F5:刷新 Esc:退出 "));
    tui_set_attr(tui_default_attr());
    tui_dt_page_flush();
}

static void pr_detail_page(int idx) {
    if (tui_dt_page_open() != 0) return;
    tui_print_at(0, 1, tui_tr("Process detail: ", "进程详情："));
    tui_print(g_rows[idx].name);
    char line[112], num[24];
    int row = 2;
    u64_to_str((u64)g_rows[idx].pid, num);
    strncpy(line, "  pid : ", sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)g_rows[idx].ppid, num);
    strncpy(line, "  ppid: ", sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)g_rows[idx].tid, num);
    strncpy(line, "  tid : ", sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str(g_rows[idx].brk >> 10, num);
    strncpy(line, "  brk : ", sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_dt_str_append(line, (int)sizeof(line), " KiB");
    tui_print_at(row++, 1, line);
    int tid = user_process_get_tid(g_rows[idx].pid);
    strncpy(line, "  kill-ready tid: ", sizeof(line) - 1);
    if (tid >= 0) {
        u64_to_str((u64)tid, num);
        tui_dt_str_append(line, (int)sizeof(line), num);
    } else {
        tui_dt_str_append(line, (int)sizeof(line), "(none)");
    }
    tui_print_at(row++, 1, line);
    tui_dt_page_wait_close(NULL);
}

int tui_dt_proc_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    pr_scan();
    for (;;) {
        pr_render();
        int k = tui_getch_timeout(600);   /* auto-refresh half-second tick */
        if (k == TUI_KEY_NONE || k == -1) { pr_scan(); continue; }
        if (k == TUI_KEY_UP)    { if (g_sel > 0) g_sel--; }
        else if (k == TUI_KEY_DOWN) { if (g_sel < g_row_count - 1) g_sel++; }
        else if (k == TUI_KEY_F5)   { pr_scan(); tui_dt_toast(1, "refreshed"); }
        else if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
        else if (k == TUI_KEY_ENTER && g_row_count > 0) {
            pr_detail_page(g_sel);      /* sub-page frees its window */
            tui_dt_page_open();         /* reopen the task page      */
        }
        else if (k == 'k' && g_row_count > 0) {
            int tid = user_process_get_tid(g_rows[g_sel].pid);
            if (tid < 0) {
                tui_dt_toast(0, "no tid for pid %d", g_rows[g_sel].pid);
            } else {
                int rc = user_process_kill_by_tid((tid_t)tid);
                tui_dt_toast(rc == 0, "kill tid=%d rc=%d", tid, rc);
                pr_scan();
            }
        }
        else if (k == 'k') tui_dt_toast(0, "no process selected");
    }
    return 0;
}
