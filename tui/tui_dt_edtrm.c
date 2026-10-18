/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_dt_edtrm.c
 * Purpose: the desktop text editor bridge and the ring-3 terminal.
 *
 *   Editor: type (or accept) a file path, the file is opened with the
 *           real nano editor through its live shell command path
 *           (the desktop session parks itself, nano runs full screen,
 *           ^X returns to the desktop).
 *   Terminal: parks the desktop, spawns the ring-3 ush (syscall-only
 *           user shell), and re-enters the desktop when ush exits.
 *           Privileged instructions from ring 3 are intercepted by
 *           the CPU (#GP) and the kernel - never a panic.
 */
#include "tui_desktop.h"
#include "../fs/fs_vfs.h"
#include "../kernel/lib/lib_string.h"
#include "../shell/shell.h"
#include "../drivers/display/screen_console.h"
#include "../drivers/input/driver_input_keyboard.h"

/* console wait + desktop re-entry (defined in tui_desktop.c) */
extern int tui_dt_console_wait_key(void);
extern int tui_dt_reenter(void);

/* ------------------------------------------------------------------ */
/* editor                                                              */
/* ------------------------------------------------------------------ */

int tui_dt_editor_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    tui_print_at(0, 1, tui_tr("Text editor", "文本编辑器"));
    tui_print_at(2, 1, tui_tr(
        "File to edit (created when missing):",
        "要编辑的文件（不存在则新建）："));
    /* argument form in the middle of the page */
    tui_window_t *fwin = tui_window_new(rows / 2 - 2, (cols - 60) / 2, 5, 60);
    if (!fwin) return -1;
    tui_window_focus_set(fwin);
    tui_window_title(fwin, "editor");
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD);
    tui_print_at(0, 1, tui_tr("nano", "nano 编辑器"));
    tui_set_attr(tui_default_attr());
    tui_field_t f[1];
    memset(f, 0, sizeof(f));
    f[0].y = 2; f[0].x = 8; f[0].width = 48;
    f[0].editable = 1;
    f[0].label = "path";
    tui_form_t *form = tui_form_new(fwin, f, 1);
    tui_form_post(form);
    tui_window_refresh(fwin);

    char path[128];
    path[0] = 0;
    int cancelled = 0;
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC) { cancelled = 1; break; }
        int d = tui_form_driver(form, k);
        if (d == 1) {
            const char *v = tui_form_field_buffer(form, 0);
            strncpy(path, v ? v : "", sizeof(path) - 1);
            path[sizeof(path) - 1] = 0;
            break;
        }
        if (d == -1) { cancelled = 1; break; }
        tui_window_refresh(fwin);
    }
    tui_form_unpost(form);
    tui_window_free(fwin);
    {
        tui_window_t *pg = tui_dt_page_window();
        if (pg) tui_window_focus_set(pg);
    }
    if (cancelled || !path[0]) return 0;   /* user changed their mind */

    /* existing file? honest hint either way */
    fs_vfs_stat_t st;
    int exists = fs_vfs_stat(path, &st) == 0;

    /* park the desktop, run the real nano, come back */
    tui_shutdown();
    screen_console_puts("\n[editor] ");
    screen_console_puts(exists ? "opening " : "creating ");
    screen_console_puts(path);
    screen_console_puts(tui_tr("  (nano: ^O save, ^X exit)\n",
                               "  （nano：^O 保存，^X 退出）\n"));
    char cmd[160];
    strncpy(cmd, "nano ", sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = 0;
    tui_dt_str_append(cmd, (int)sizeof(cmd), path);
    int rc = shell_execute(cmd);
    screen_console_puts(tui_tr(
        "\n[editor] closed - press any key for the desktop...",
        "\n[编辑器] 已关闭 - 按任意键返回桌面..."));
    tui_dt_console_wait_key();
    if (tui_dt_reenter() != 0) return rc;
    tui_dt_toast(1, "editor rc=%d", rc);
    return rc;
}

/* ------------------------------------------------------------------ */
/* ring-3 terminal                                                     */
/* ------------------------------------------------------------------ */

int tui_dt_terminal_run(tui_dt_node_t *self) {
    (void)self;
    tui_shutdown();
    screen_console_puts("\n[terminal] ring-3 user shell (ush)\n");
    screen_console_puts(tui_tr(
        "  every command runs in ring 3 through syscalls - privileged\n"
        "  instructions fault and are contained, the kernel stays up.\n"
        "  type 'exit' to return to the desktop.\n\n",
        "  所有命令均在 Ring 3 通过 syscall 执行 - 特权指令触发异常\n"
        "  并被隔离，内核不受影响。\n"
        "  输入 'exit' 返回桌面。\n\n"));
    int rc = shell_execute("run ush");
    screen_console_puts(tui_tr(
        "\n[terminal] ush exited (rc=",
        "\n[终端] ush 已退出 (rc="));
    char num[12];
    u64_to_str((u64)(rc < 0 ? 0 : (u32)rc), num);
    screen_console_puts(num);
    screen_console_puts(tui_tr(") - press any key for the desktop...",
                               ") - 按任意键返回桌面..."));
    tui_dt_console_wait_key();
    if (tui_dt_reenter() != 0) return rc;
    tui_dt_toast(1, "terminal rc=%d", rc);
    return rc;
}
