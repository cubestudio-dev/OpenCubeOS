/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_cmd.c
 * Purpose: The shell entry points.  `tui` is THE single desktop entry
 *          (WP-10-tui-unified): it reads shell.lang, renders the dynamic
 *          menu tree and is the only launcher (tui-cn was removed).
 *          tui-test / tui-hw / tui-net / tui-sysinfo / tui-time /
 *          tui-drv / tui-sys / tui-ext stay exactly as they were.
 */
#include "tui.h"
#include "tui_desktop.h"
#include "../shell/shell.h"
#include "../kernel/lib/lib_string.h"

int tui_test_run(int verbose);
int tui_time_main(void);
void tui_time_register_pages(void);
int tui_sys_main(void);
void tui_sys_register_pages(void);

static int shell_cmd_tui(const char *args) {
    (void)args;
    /* WP-10-tui-unified: one desktop for both languages - it reads
     * shell.lang at start, every label is per-language and F2 switches
     * (and persists) the language at runtime. */
    return tui_desktop_main();
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
