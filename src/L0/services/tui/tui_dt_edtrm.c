/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_dt_edtrm.c; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-native
 * File: tui/tui_dt_edtrm.c
 * Purpose: the desktop ring-3 terminal page.  The former nano command
 *          bridge was REMOVED in WP-10-tui-native - the editor is a
 *          native TUI application now (tui_dt_editor.c).  This file
 *          keeps the terminal: it parks the desktop, spawns the
 *          ring-3 ush (syscall-only user shell), and re-enters the
 *          desktop when ush exits.  Privileged instructions from
 *          ring 3 are intercepted by the CPU (#GP) and the kernel -
 *          never a panic.
 */
#include "tui_desktop.h"
#include "lib_string.h"
#include "tui_exec.h"
#include "screen_console.h"
#include "driver_input_keyboard.h"

/* console wait + desktop re-entry (defined in tui_desktop.c) */
extern int tui_dt_console_wait_key(void);
extern int tui_dt_reenter(void);

/* ------------------------------------------------------------------ */
/* ring-3 terminal                                                     */
/* ------------------------------------------------------------------ */

int tui_dt_terminal_run(tui_dt_node_t *self) {
    (void)self;
    tui_shutdown();
    screen_console_puts("\n");
    screen_console_puts(tui_tr("[terminal] ring-3 user shell (ush)\n",
                               "[终端] Ring 3 用户 shell（ush）\n"));
    screen_console_puts(tui_tr(
        "  every command runs in ring 3 through syscalls - privileged\n"
        "  instructions fault and are contained, the kernel stays up.\n"
        "  type 'exit' to return to the desktop.\n\n",
        "  所有命令均在 Ring 3 通过 syscall 执行 - 特权指令触发异常\n"
        "  并被隔离，内核不受影响。\n"
        "  输入 'exit' 返回桌面。\n\n"));
    int rc = tui_exec_interactive("run ush");
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
    tui_dt_toast(1, tui_tr("terminal rc=%d", "终端 rc=%d"), rc);
    return rc;
}
