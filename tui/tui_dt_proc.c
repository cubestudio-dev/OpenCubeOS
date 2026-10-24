/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-native
 * File: tui/tui_dt_proc.c
 * Purpose: the desktop task manager - live process table (g_procs),
 *          real PMM memory statistics, uptime, and Kill.  F5 refreshes;
 *          Enter opens a per-process detail page; k kills the selected
 *          process through user_process_kill (the same path `kill`
 *          uses).  All data is read from the real subsystems.
 *
 * WP-10-tui-native upgrades:
 *   - STATE and CPU(ms) columns: the live scheduler table
 *     (core_sched_task_info_get) is joined by tid, so every user
 *     process shows its real READY/RUNNING/BLOCKED/EXITED state and
 *     its accumulated cpu_time_ticks (10 ms each).
 *   - Kill asks for confirmation (tui_dt_confirm) before the
 *     destructive syscall path runs.
 *   - up to 32 rows with a scrolling viewport (was a hard 12-row cap).
 *   - every toast, column header and detail label is bilingual.
 */
#include "tui_desktop.h"
#include "../core/lib/lib_string.h"
#include "../core/core_usermode.h"
#include "../core/core_sched.h"
#include "../core/core_timer.h"
#include "../core/mem/mem_pmm.h"
#include "../drivers/input/driver_input_keyboard.h"

#define PR_MAX_ROWS 32

/* collect the alive processes into a flat view */
typedef struct {
    pid_t pid;
    pid_t ppid;
    tid_t tid;
    u64   brk;
    u64   cpu_ticks;
    int   state;                 /* scheduler TASK_* state               */
    char  name[32];
} pr_row_t;

static pr_row_t g_rows[PR_MAX_ROWS];
static int      g_row_count = 0;
static int      g_sel = 0;
static int      g_top = 0;       /* viewport top row                     */

static const char *pr_state_str(int s) {
    switch (s) {
    case TASK_READY:   return tui_tr("READY", "就绪");
    case TASK_RUNNING: return tui_tr("RUN", "运行");
    case TASK_BLOCKED: return tui_tr("BLOCK", "阻塞");
    case TASK_EXITED:  return tui_tr("EXIT", "退出");
    default:           return "?";
    }
}

static void pr_scan(void) {
    /* scheduler table first: state + cpu ticks per tid */
    core_sched_task_info_t ti[64];
    int tn = core_sched_task_info_get(ti, 64);
    g_row_count = 0;
    for (int i = 0; i < MAX_USER_PROCS && g_row_count < PR_MAX_ROWS; i++) {
        if (!g_procs[i].alive) continue;
        g_rows[g_row_count].pid = g_procs[i].pid;
        g_rows[g_row_count].ppid = g_procs[i].ppid;
        g_rows[g_row_count].tid = g_procs[i].tid;
        g_rows[g_row_count].brk = g_procs[i].brk;
        g_rows[g_row_count].cpu_ticks = 0;
        g_rows[g_row_count].state = TASK_READY;
        for (int t = 0; t < tn; t++) {
            if (ti[t].tid == g_procs[i].tid) {
                g_rows[g_row_count].cpu_ticks = ti[t].cpu_time_ticks;
                g_rows[g_row_count].state = ti[t].state;
                break;
            }
        }
        strncpy(g_rows[g_row_count].name, g_procs[i].name, 31);
        g_rows[g_row_count].name[31] = 0;
        g_row_count++;
    }
    if (g_sel >= g_row_count) g_sel = g_row_count ? g_row_count - 1 : 0;
    if (g_sel < 0) g_sel = 0;
    if (g_top > g_sel) g_top = g_sel;
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
        "PID   PPID  TID   BRK(KiB)  CPU(ms)  STATE   NAME",
        "PID   PPID  TID   BRK(KiB)  CPU(毫秒) 状态    名称"));
    tui_set_attr(tui_default_attr());

    /* viewport (scrolling over up to 32 collected rows) */
    int max_items = rows - 3 - 2;
    if (max_items < 1) max_items = 1;
    if (g_top > g_row_count - 1) g_top = g_row_count ? g_row_count - 1 : 0;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + max_items) g_top = g_sel - max_items + 1;
    for (int r = 3; r < 3 + max_items && r < rows - 2; r++) {
        tui_set_attr(tui_default_attr());
        for (int x = 0; x < cols; x++) tui_print_at(r, x, " ");
    }
    for (int i = 0; i < g_row_count; i++) {
        int row = 3 + i - g_top;
        if (row < 3 || row >= 3 + max_items) continue;
        tui_set_attr(i == g_sel
            ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_SILVER) | TUI_ATTR_BOLD)
            : TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        char rowbuf[112];
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
        u64_to_str(g_rows[i].cpu_ticks * 10, num2);
        for (int k = 0; num2[k]; k++) rowbuf[n++] = num2[k];
        while (n < 37) rowbuf[n++] = ' ';
        const char *sst = pr_state_str(g_rows[i].state);
        for (const char *p = sst; *p && n < 44; p++) rowbuf[n++] = *p;
        while (n < 44) rowbuf[n++] = ' ';
        for (int k = 0; g_rows[i].name[k] && n < 78; k++) rowbuf[n++] = g_rows[i].name[k];
        rowbuf[n] = 0;
        tui_print_at(row, 1, rowbuf);
    }
    if (g_row_count == 0) {
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        tui_print_at(3, 1, tui_tr(
            "  (no user processes alive)",
            "  （当前无存活用户进程）"));
    } else if (g_row_count > max_items) {
        char sb[64];
        strncpy(sb, tui_tr("  rows ", "  行 "), sizeof(sb) - 1);
        char num[8];
        u64_to_str((u64)(g_top + 1), num);
        tui_dt_str_append(sb, (int)sizeof(sb), num);
        tui_dt_str_append(sb, (int)sizeof(sb), "-");
        u64_to_str((u64)(g_top + max_items < g_row_count
                          ? g_top + max_items : g_row_count), num);
        tui_dt_str_append(sb, (int)sizeof(sb), num);
        tui_dt_str_append(sb, (int)sizeof(sb), "/");
        u64_to_str((u64)g_row_count, num);
        tui_dt_str_append(sb, (int)sizeof(sb), num);
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
        tui_print_at(rows - 3, 1, sb);
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
    strncpy(line, tui_tr("  pid : ", "  pid ："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)g_rows[idx].ppid, num);
    strncpy(line, tui_tr("  ppid: ", "  ppid："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)g_rows[idx].tid, num);
    strncpy(line, tui_tr("  tid : ", "  tid ："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str(g_rows[idx].brk >> 10, num);
    strncpy(line, tui_tr("  brk : ", "  内存："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_dt_str_append(line, (int)sizeof(line), tui_tr(" KiB", " KiB"));
    tui_print_at(row++, 1, line);
    u64_to_str(g_rows[idx].cpu_ticks * 10, num);
    strncpy(line, tui_tr("  cpu : ", "  CPU ："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_dt_str_append(line, (int)sizeof(line), tui_tr(" ms (scheduler ticks)",
                                                     " 毫秒（调度器节拍）"));
    tui_print_at(row++, 1, line);
    strncpy(line, tui_tr("  state: ", "  状态："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), pr_state_str(g_rows[idx].state));
    tui_print_at(row++, 1, line);
    int tid = user_process_get_tid(g_rows[idx].pid);
    strncpy(line, tui_tr("  kill-ready tid: ", "  可结束 tid："), sizeof(line) - 1);
    if (tid >= 0) {
        u64_to_str((u64)tid, num);
        tui_dt_str_append(line, (int)sizeof(line), num);
    } else {
        tui_dt_str_append(line, (int)sizeof(line), tui_tr("(none)", "（无）"));
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
        else if (k == TUI_KEY_PGUP) { g_sel -= 8; if (g_sel < 0) g_sel = 0; }
        else if (k == TUI_KEY_PGDN) { g_sel += 8;
            if (g_sel >= g_row_count) g_sel = g_row_count ? g_row_count - 1 : 0; }
        else if (k == TUI_KEY_F5) {
            pr_scan();
            tui_dt_toast(1, tui_tr("refreshed", "已刷新"));
        }
        else if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
        else if (k == TUI_KEY_ENTER && g_row_count > 0) {
            pr_detail_page(g_sel);      /* sub-page frees its window */
            tui_dt_page_open();         /* reopen the task page      */
        }
        else if (k == 'k' && g_row_count > 0) {
            /* WP-10-tui-native: Kill asks first - destructive action */
            char msg[160];
            strncpy(msg, tui_tr("kill process ", "结束进程 "), sizeof(msg) - 1);
            tui_dt_str_append(msg, (int)sizeof(msg), g_rows[g_sel].name);
            tui_dt_str_append(msg, (int)sizeof(msg), tui_tr(" (pid ", "（pid "));
            char num[12];
            u64_to_str((u64)g_rows[g_sel].pid, num);
            tui_dt_str_append(msg, (int)sizeof(msg), num);
            tui_dt_str_append(msg, (int)sizeof(msg), ") ?");
            if (tui_dt_confirm(msg, msg) != 1) {
                tui_dt_toast(1, tui_tr("cancelled", "已取消"));
                continue;
            }
            int tid = user_process_get_tid(g_rows[g_sel].pid);
            if (tid < 0) {
                tui_dt_toast(0, tui_tr("no tid for pid %d", "pid %d 无 tid"),
                             g_rows[g_sel].pid);
            } else {
                int rc = user_process_kill_by_tid((tid_t)tid);
                tui_dt_toast(rc == 0, tui_tr("kill tid=%d rc=%d",
                                             "结束 tid=%d rc=%d"), tid, rc);
                pr_scan();
            }
        }
        else if (k == 'k') {
            tui_dt_toast(0, tui_tr("no process selected", "未选择进程"));
        }
    }
    return 0;
}
