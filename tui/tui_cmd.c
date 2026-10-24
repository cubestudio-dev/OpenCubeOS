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
#include "../recovery/shell.h"
#include "../recovery/shell_lang.h"
#include "../core/lib/lib_string.h"
#include "core_registry.h"

int tui_test_run(int verbose);
int tui_time_main(void);
void tui_time_register_pages(void);
int tui_sys_main(void);
void tui_sys_register_pages(void);

/* ---- WP-10-tui-qa: language-aware classic-page launchers ----
 * The Chinese twins (chinese/tui_cn_hw/net/sysinfo.c) walk the same
 * real kernel data sources as the English pages; until now nothing
 * called them (the tui-cn launcher was removed at tui-unified and
 * the externs in chinese_cmd.c stayed dangling).  Dispatch on
 * shell.lang so a zh user gets the Chinese pages everywhere the
 * classic apps are launched. */
int tui_cn_hw_main(void);
int tui_cn_net_main(void);
int tui_cn_sysinfo_main(void);

int tui_hw_app(void) {
    return (shell_lang_get() == SHELL_LANG_ZH)
         ? tui_cn_hw_main() : tui_hw_main();
}
int tui_net_app(void) {
    return (shell_lang_get() == SHELL_LANG_ZH)
         ? tui_cn_net_main() : tui_net_main();
}
int tui_sysinfo_app(void) {
    return (shell_lang_get() == SHELL_LANG_ZH)
         ? tui_cn_sysinfo_main() : tui_sysinfo_main();
}

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
    int rc = tui_hw_app();
    tui_shutdown();
    return rc;
}

static int shell_cmd_tui_net(const char *args) {
    (void)args;
    tui_init();
    int rc = tui_net_app();
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
    int rc = tui_sysinfo_app();
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
    int rc = tui_hw_app();
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


/* ---- v2.0.0 file-local command declarations ---- */
OC_SHELL_CMD("tui", shell_cmd_tui, "full-screen TUI main menu (arrows/Tab/Enter/Esc)", "display");
OC_SHELL_CMD("tui-test", shell_cmd_tui_test, "run the TUI self-test suite (input/expected/actual/verdict)", "display");
OC_SHELL_CMD("tui-hw", shell_cmd_tui_hw, "hardware configuration TUI (optional: page name)", "display");
OC_SHELL_CMD("tui-net", shell_cmd_tui_net, "wireless network TUI (WiFi + Bluetooth)", "display");
OC_SHELL_CMD("tui-sysinfo", shell_cmd_tui_sysinfo, "system information TUI (optional: page name)", "display");
OC_SHELL_CMD("tui-time", shell_cmd_tui_time, "time TUI: clock/sources/set RTC/timezone", "display");
OC_SHELL_CMD("tui-drv", shell_cmd_tui_drv, "driver setup TUI (alias of tui-hw; page name optional)", "display");
OC_SHELL_CMD("tui-sys", shell_cmd_tui_sys, "system settings TUI (host/net/storage/user/security/update/log/tui)", "display");
OC_SHELL_CMD("tui-ext", shell_cmd_tui_ext, "TUI L1 extension lab: selftest + registries", "display");
