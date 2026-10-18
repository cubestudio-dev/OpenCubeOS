/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_dt_apps.c
 * Purpose: desktop builtin pages: full-screen page helper, help (F1),
 *          language switch, update.  All pages render real data - no
 *          stubs.  (The command bridge and the dynamic disk subtree
 *          live in tui_dt_cmdbr.c.)
 */
#include "tui_desktop.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/lib/lib_config.h"
#include "../shell/shell.h"

/* ------------------------------------------------------------------ */
/* help page (F1)                                                      */
/* ------------------------------------------------------------------ */

typedef struct { const char *key; const char *en; const char *zh; } dt_kv_t;

int tui_dt_help_page_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, tui_tr("Open Cube OS Desktop - keys",
                              "Open Cube OS 桌面 - 按键"));
    static const dt_kv_t kvs[] = {
        { "Up/Down (Ctrl-P/N)", "move the selection",        "移动选择" },
        { "Enter",              "open the selected item",    "打开选中项" },
        { "Esc / Backspace",    "back one level; at root: exit", "返回上一层；根层退出" },
        { "F1",                 "this help",                 "帮助" },
        { "F2",                 "switch en/zh (persists)",   "切换 en/zh（持久化）" },
        { "F5",                 "repaint / refresh",         "刷新重绘" },
        { "F10",                "exit the desktop",          "退出桌面" },
        { NULL, NULL, NULL },
    };
    int row = 2;
    for (int i = 0; kvs[i].key; i++) {
        char line[128];
        int n = 0;
        for (const char *p = kvs[i].key; *p && n < 30; p++) line[n++] = *p;
        while (n < 22) line[n++] = ' ';
        line[n++] = '-'; line[n++] = ' ';
        const char *d = tui_tr(kvs[i].en, kvs[i].zh);
        for (const char *p = d; *p && n < 120; p++) line[n++] = *p;
        line[n] = 0;
        tui_print_at(row++, 1, line);
    }
    row++;
    tui_print_at(row++, 1, tui_tr("Registered top-level applications:",
                                  "已注册的顶层应用："));
    tui_dt_node_t *root = tui_dt_find("root");
    for (int i = 0; root && i < root->child_count &&
                    row < tui_dt_page_rows() - 2; i++) {
        tui_dt_node_t *c = root->children[i];
        if (!c) continue;
        char line[128];
        int n = 0;
        for (const char *p = c->en; *p && n < 24; p++) line[n++] = *p;
        while (n < 18) line[n++] = ' ';
        line[n++] = '-'; line[n++] = ' ';
        const char *d = tui_tr(c->desc_en, c->desc_zh);
        for (const char *p = d; *p && n < 120; p++) line[n++] = *p;
        line[n] = 0;
        tui_print_at(row++, 1, line);
    }
    tui_dt_page_wait_close(NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* language switch page                                                */
/* ------------------------------------------------------------------ */

int tui_dt_lang_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, "Language / 语言");
    int row = 2;
    tui_print_at(row++, 1, tui_tr("1. English (en)", "1. 英语 (en)"));
    tui_print_at(row++, 1, tui_tr("2. Chinese (zh)", "2. 中文 (zh)"));
    tui_print_at(row++, 1, tui_tr(
        "Press 1 or 2; Esc keeps the current language.",
        "按 1 或 2；Esc 保持当前语言。"));
    tui_dt_page_flush();
    for (;;) {
        int k = tui_getch();
        if (k == '1') { tui_lang_set(SHELL_LANG_EN); break; }
        if (k == '2') { tui_lang_set(SHELL_LANG_ZH); break; }
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* update page (real commands, live captured output)                   */
/* ------------------------------------------------------------------ */

/* print the first lines of a captured buffer, wrapping at the screen */
static void dt_print_captured(const char *out, int start_row, int max_rows) {
    int printed = 0;
    const char *p = out;
    while (*p && printed < max_rows) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > tui_dt_page_cols() - 3) len = tui_dt_page_cols() - 3;
        char line[128];
        int m = len < 120 ? len : 120;
        memcpy(line, p, m);
        line[m] = 0;
        tui_print_at(start_row + printed, 1, line);
        printed++;
        if (!nl) break;
        p = nl + 1;
    }
}

int tui_dt_update_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, tui_tr("Update", "更新"));
    tui_print_at(2, 1, tui_tr("1. checkupdate (network check vs update.json)",
                              "1. checkupdate（联网检查 update.json）"));
    tui_print_at(3, 1, tui_tr("2. abupdate   (A/B slot status)",
                              "2. abupdate   （A/B 槽状态）"));
    tui_print_at(4, 1, tui_tr("Esc cancels.", "Esc 取消。"));
    tui_dt_page_flush();
    for (;;) {
        int k = tui_getch();
        if (k == '1' || k == '2') {
            char out[8192];
            const char *cmd = (k == '1') ? "checkupdate" : "abupdate";
            tui_dt_loading_begin(cmd);
            int rc = shell_execute_captured(cmd, out, (int)sizeof(out));
            tui_dt_loading_end(rc == 0, "update check done",
                               "update check failed");
            tui_dt_page_open();          /* fresh window over the toast */
            tui_print_at(0, 1, cmd);
            dt_print_captured(out, 2, tui_dt_page_rows() - 5);
            tui_dt_toast(rc == 0, "%s rc=%d", cmd, rc);
            tui_dt_page_wait_close(NULL);
            return rc;
        }
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') return 0;
    }
}
