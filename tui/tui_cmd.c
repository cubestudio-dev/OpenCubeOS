/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_cmd.c
 * Purpose: The shell entry points: tui (main menu), tui-test,
 *          tui-hw, tui-net, tui-sysinfo.  All five are registered
 *          through the standard shell registry with a WP tag.
 */
#include "tui.h"
#include "../shell/shell.h"
#include "../shell/shell_lang.h"   /* fix3: shell.lang drives the launcher choice */
#include "../chinese/tui_chinese.h" /* fix3: tui_cn_launcher() */
#include "../kernel/lib/lib_string.h"

int tui_test_run(int verbose);
int tui_time_main(void);
void tui_time_register_pages(void);
int tui_sys_main(void);
void tui_sys_register_pages(void);

/* fix4: main menu = the 4 built-in applications + every entry from the
 * fix4 L1 menu extension registry (sorted by priority). "System
 * settings" registers itself through the same registry, so the menu
 * and `tui-sys` agree. */
static int tui_main_app(void) {
    if (tui_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) {
        /* fix2: tui_init() already cleared the screen and hid the
         * cursor - returning without tui_shutdown() left the session
         * "open" (g_started=1, cursor hidden) and the shell with a
         * wiped screen. */
        tui_shutdown();
        return -1;
    }
    tui_window_title(win, "Open Cube OS - TUI (WP-10-tui)");
    tui_menuext_item_t ext[TUI_EXT_MENU_MAX];
    int extn = tui_menu_list(ext, TUI_EXT_MENU_MAX);
    tui_menu_item_t items[5 + TUI_EXT_MENU_MAX + 1];
    items[0].name = "Hardware configuration";
    items[0].desc = "CPU/memory/storage/network/sound/USB/PCI/input/camera/wireless/timers/display";
    items[1].name = "Wireless network";
    items[1].desc = "WiFi scan and connect, Bluetooth manager";
    items[2].name = "System information";
    items[2].desc = "overview, processes, logs, monitor, power, update";
    items[3].name = "Help";
    items[3].desc = "shortcut reference (F1 anywhere)";
    items[4].name = "Exit";
    items[4].desc = "leave the TUI";
    for (int i = 0; i < extn; i++) {
        items[5 + i].name = ext[i].name;
        items[5 + i].desc = ext[i].desc;
    }
    int itemn = 5 + extn;
    tui_menu_t *menu = tui_menu_new(win, items, itemn);
    tui_menu_post(menu);
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_F1) { tui_sysinfo_help(); tui_menu_post(menu); continue; }
        if (k == TUI_KEY_F(5)) { tui_update_panels(); tui_menu_post(menu); continue; }
        int d = tui_menu_driver(menu, k);
        if (d == -1) break;
        if (d == 1) {
            int sel = tui_menu_current(menu);
            if (sel == 4) break;
            if (sel == 0) tui_hw_main();
            else if (sel == 1) tui_net_main();
            else if (sel == 2) tui_sysinfo_main();
            else if (sel == 3) tui_sysinfo_help();
            else {
                /* extension entry: run by name */
                tui_menuext_item_t cur[TUI_EXT_MENU_MAX];
                int n = tui_menu_list(cur, TUI_EXT_MENU_MAX);
                int idx = sel - 5;
                if (idx >= 0 && idx < n && cur[idx].action) cur[idx].action();
            }
            tui_menu_post(menu);
        }
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_shutdown();
    return 0;
}

static int shell_cmd_tui(const char *args) {
    (void)args;
    /* fix3: with shell.lang=zh the user asked for the Chinese system
     * experience - open the Chinese launcher directly (same page set
     * as `tui-cn`).  Switch back with `lang en` or
     * `config set shell.lang en`. */
    if (shell_lang_get() == SHELL_LANG_ZH)
        return tui_cn_launcher();
    return tui_main_app();
}

static int shell_cmd_tui_test(const char *args) {
    int failed = tui_test_run(args && args[0] == 'v');
    return failed;                   /* exit code = failure count */
}

static int shell_cmd_tui_hw(const char *args) {
    if (args && args[0]) {
        tui_init();
        int rc = tui_hw_run_page(args);
        tui_shutdown();
        return rc;
    }
    tui_init();
    int rc = tui_hw_main();
    tui_shutdown();
    return rc;
}

static int shell_cmd_tui_net(const char *args) {
    (void)args;
    tui_init();
    int rc = tui_net_main();
    tui_shutdown();
    return rc;
}

static int shell_cmd_tui_sysinfo(const char *args) {
    if (args && args[0]) {
        /* direct page access: overview|processes|logs|monitor|power|update|time|help */
        tui_init();
        int rc = 0;
        if (strcmp(args, "overview") == 0) rc = tui_sysinfo_overview();
        else if (strcmp(args, "processes") == 0) rc = tui_sysinfo_processes();
        else if (strcmp(args, "logs") == 0) rc = tui_sysinfo_logs();
        else if (strcmp(args, "monitor") == 0) rc = tui_sysinfo_monitor();
        else if (strcmp(args, "power") == 0) rc = tui_sysinfo_power();
        else if (strcmp(args, "update") == 0) rc = tui_sysinfo_update();
        else if (strcmp(args, "time") == 0) rc = tui_sysinfo_time();
        else if (strcmp(args, "help") == 0) rc = tui_sysinfo_help();
        else rc = -1;
        tui_shutdown();
        return rc;
    }
    tui_init();
    int rc = tui_sysinfo_main();
    tui_shutdown();
    return rc;
}

static int shell_cmd_tui_time(const char *args) {
    (void)args;
    tui_init();
    int rc = tui_time_main();
    tui_shutdown();
    return rc;
}

static int shell_cmd_tui_drv(const char *args) {
    if (args && args[0]) return shell_cmd_tui_hw(args);  /* alias: tui-drv PCI == tui-hw PCI */
    tui_init();
    int rc = tui_hw_main();
    tui_shutdown();
    return rc;
}

static int shell_cmd_tui_sys(const char *args) {
    if (args && args[0]) {
        tui_init();
        int rc = tui_ext_page_main(args);
        tui_shutdown();
        return rc;
    }
    tui_init();
    int rc = tui_sys_main();
    tui_shutdown();
    return rc;
}

/* fix4: TUI L1 extension lab - selftest + live registries listing */
static int shell_cmd_tui_ext(const char *args) {
    tui_init();
    tui_ext_page_main("ext");
    (void)args;
    tui_shutdown();
    return 0;
}

void tui_shell_cmds_register(void) {
    tui_time_register_pages();   /* fix4: time.clock/set/tz pages */
    tui_sys_register_pages();    /* fix4: sys.* pages + menu entry */
    tui_ext_register_ext_page(); /* fix4: ext lab page */
    shell_register_command_ex("tui", shell_cmd_tui,
                              "full-screen TUI main menu (arrows/Tab/Enter/Esc)",
                              "WP-10-tui");
    shell_register_command_ex("tui-test", shell_cmd_tui_test,
                              "run the TUI self-test suite (input/expected/actual/verdict)",
                              "WP-10-tui");
    shell_register_command_ex("tui-hw", shell_cmd_tui_hw,
                              "hardware configuration TUI (optional: page name)",
                              "WP-10-tui");
    shell_register_command_ex("tui-net", shell_cmd_tui_net,
                              "wireless network TUI (WiFi + Bluetooth)",
                              "WP-10-tui");
    shell_register_command_ex("tui-sysinfo", shell_cmd_tui_sysinfo,
                              "system information TUI (optional: page name)",
                              "WP-10-tui");
    shell_register_command_ex("tui-time", shell_cmd_tui_time,
                              "time TUI: clock/sources/set RTC/timezone",
                              "WP-10-chinese_support-fix4");
    shell_register_command_ex("tui-drv", shell_cmd_tui_drv,
                              "driver setup TUI (alias of tui-hw; page name optional)",
                              "WP-10-chinese_support-fix4");
    shell_register_command_ex("tui-sys", shell_cmd_tui_sys,
                              "system settings TUI (host/net/storage/user/security/update/log/tui)",
                              "WP-10-chinese_support-fix4");
    shell_register_command_ex("tui-ext", shell_cmd_tui_ext,
                              "TUI L1 extension lab: selftest + registries",
                              "WP-10-chinese_support-fix4");
}
