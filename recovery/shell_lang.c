/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support-fix3
 * File: shell/shell_lang.c
 * Purpose: bilingual system-message table + the `lang` shell command.
 *
 * shell.lang=en (default)  -> every message below serves its English
 *                             text, byte-identical to the pre-fix3
 *                             strings (the regression suite depends on
 *                             the exact wording).
 * shell.lang=zh            -> system-level shell messages switch to the
 *                             Chinese column.  The zh strings are UTF-8
 *                             and render through console_cn (active
 *                             since boot step 3), on the framebuffer
 *                             AND on the serial mirror, and through
 *                             tui_print inside the TUI.
 *
 * `lang` with no argument prints the current language; `lang en|zh`
 * switches the in-memory state AND persists it to /etc/opencube.conf
 * via lib_config_write (survives reboot on the FAT config volume).
 */
#include "../core/types.h"
#include "shell.h"
#include "shell_lang.h"
#include "../core/lib/lib_config.h"
#include "../drivers/display/screen_console.h"
#include "config_gen.h"   /* WP-13 multi-config */
#if OC_CONFIG_CHINESE
#include "../chinese/console_cn.h"
#endif
#include "../core/lib/lib_string.h"
#include "version_gen.h"   /* WP-10-tui-qa: version recompile trigger (Makefile note) */

static int g_shell_lang = SHELL_LANG_EN;

int shell_lang_get(void) {
    return g_shell_lang;
}

void shell_lang_set(int lang) {
    g_shell_lang = (lang == SHELL_LANG_ZH) ? SHELL_LANG_ZH : SHELL_LANG_EN;
}

void shell_lang_set_str(const char *s) {
    if (s && s[0] == 'z' && s[1] == 'h' && (s[2] == 0 || s[2] == ' ')) {
        g_shell_lang = SHELL_LANG_ZH;
        return;
    }
    if (s && s[0] == 'e' && s[1] == 'n' && (s[2] == 0 || s[2] == ' ')) {
        g_shell_lang = SHELL_LANG_EN;
        return;
    }
    g_shell_lang = SHELL_LANG_EN;
}

const char *shell_lang_name(void) {
    return (g_shell_lang == SHELL_LANG_ZH) ? "zh" : "en";
}

/* -------- bilingual table ------------------------------------------ */
typedef struct {
    const char *en;
    const char *zh;
} shell_msg_entry_t;

static const shell_msg_entry_t g_msgs[SM_COUNT] = {
    /* SM_READY */
    { "Open Cube OS " OC_RELEASE_VERSION " ready. Type 'help' for commands.",
      "Open Cube OS " OC_RELEASE_VERSION " 已就绪。输入 'help' 查看命令。" },
    /* SM_TRY */
    { "(Try: dhcp, ping 10.0.2.2, wget 10.0.2.2, dns example.com, route, firewall, tcpstats)",
      "（试试：dhcp、ping 10.0.2.2、wget 10.0.2.2、dns example.com、route、firewall、tcpstats）" },
    /* SM_HELP_AZ */
    { "Available commands (A-Z):",
      "可用命令（A-Z）：" },
    /* SM_HELP_AZ_TAIL */
    { " commands. Use 'help -w' for the work-package view.",
      " 个命令。输入 'help -w' 查看按工作包分组视图。" },
    /* SM_HELP_WP */
    { "Commands by work package:",
      "按工作包分组的命令：" },
    /* SM_HELP_WP_TAIL */
    { " commands.",
      " 个命令。" },
    /* SM_TOTAL */
    { "Total: ",
      "总计：" },
    /* SM_OTHER_BUCKET */
    { "=== other (commands with unregistered work-package tags) ===",
      "=== other（未登记工作包标签的命令）===" },
    /* SM_UNKNOWN_PRE */
    { "unknown command: ",
      "未知命令：" },
    /* SM_UNKNOWN_POST */
    { " (try 'help')",
      "（试试 'help'）" },
    /* SM_LANG_CUR */
    { "shell output language: ",
      "Shell 输出语言：" },
    /* SM_LANG_SET */
    { "shell output language set to ",
      "Shell 输出语言已设为 " },
    /* SM_LANG_USAGE */
    { "usage: lang [en|zh]  (no argument shows the current language)",
      "用法：lang [en|zh]（无参数显示当前语言）" },
    /* SM_CFG_SAVED */
    { "config: saved",
      "配置已保存" },
    /* SM_PERSIST_OK (fix3: write landed on the FAT32 /etc volume) */
    { " (persisted)",
      "（已写入配置卷，重启后保留）" },
    /* SM_PERSIST_RAM (fix3: ramfs fallback - not persistent) */
    { " (in-memory only; ramfs config, lost on reboot)",
      "（仅内存；配置卷为 ramfs，重启后丢失）" },
};

const char *shell_msg(int id) {
    if (id < 0 || id >= SM_COUNT)
        return "";
    return (g_shell_lang == SHELL_LANG_ZH) ? g_msgs[id].zh : g_msgs[id].en;
}

/* -------- lang command --------------------------------------------- */
int shell_cmd_lang(const char *args) {
    if (!args || !args[0]) {
        console_cn_puts(shell_msg(SM_LANG_CUR));
        console_cn_puts(shell_lang_name());
        console_cn_putcw('\n');
        return 0;
    }
    /* trim the first token */
    char tok[8];
    int n = 0;
    const char *p = args;
    while (*p == ' ') p++;
    while (p[n] && p[n] != ' ' && n < 7) { tok[n] = p[n]; n++; }
    tok[n] = 0;
    if ((tok[0] == 'e' && tok[1] == 'n' && tok[2] == 0) ||
        (tok[0] == 'z' && tok[1] == 'h' && tok[2] == 0)) {
        shell_lang_set_str(tok);
        int wr = lib_config_write("shell.lang", tok);
        console_cn_puts(shell_msg(SM_LANG_SET));
        console_cn_puts(tok);
        if (wr == 0) {
            /* fix3: honest persistence report - the /etc volume can be
             * FAT32 (edits survive) or the ramfs fallback (lost on
             * reboot); rc==0 alone used to claim "persisted" either way. */
            console_cn_puts(shell_msg(lib_config_backend_fat() == 1
                                          ? SM_PERSIST_OK
                                          : SM_PERSIST_RAM));
        } else {
            console_cn_puts(" (in-memory only; config write rc=");
            char num[12];
            u64_to_str((u64)(wr < 0 ? -wr : wr), num);
            console_cn_puts(num);
            console_cn_puts(")");
        }
        console_cn_putcw('\n');
        return 0;
    }
    console_cn_puts(shell_msg(SM_LANG_USAGE));
    console_cn_putcw('\n');
    return 1;
}
