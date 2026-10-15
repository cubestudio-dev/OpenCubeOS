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
#include "../kernel/lib/lib_string.h"

int tui_test_run(int verbose);

/* main menu app: jump into any of the three TUI applications */
static int tui_main_app(void) {
    if (tui_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Open Cube OS - TUI (WP-10-tui)");
    tui_menu_item_t items[5];
    items[0].name = "Hardware configuration";
    items[0].desc = "CPU/memory/storage/network/sound/USB/PCI/input/camera/wireless/timers";
    items[1].name = "Wireless network";
    items[1].desc = "WiFi scan and connect, Bluetooth manager";
    items[2].name = "System information";
    items[2].desc = "overview, processes, logs, monitor, power, update";
    items[3].name = "Help";
    items[3].desc = "shortcut reference (F1 anywhere)";
    items[4].name = "Exit";
    items[4].desc = "leave the TUI";
    tui_menu_t *menu = tui_menu_new(win, items, 5);
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

void tui_cmd_register(void) {
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
}
