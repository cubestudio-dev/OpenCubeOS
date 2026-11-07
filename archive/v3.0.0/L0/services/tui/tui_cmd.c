/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_cmd.c; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
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
#include "tui_exec.h"
#include "disp_lang.h"
#include "lib_string.h"
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
    return (disp_lang_get() == DISP_LANG_ZH)
         ? tui_cn_hw_main() : tui_hw_main();
}
int tui_net_app(void) {
    return (disp_lang_get() == DISP_LANG_ZH)
         ? tui_cn_net_main() : tui_net_main();
}
int tui_sysinfo_app(void) {
    return (disp_lang_get() == DISP_LANG_ZH)
         ? tui_cn_sysinfo_main() : tui_sysinfo_main();
}

int shell_cmd_tui(const char *args) {
    (void)args;
    /* WP-10-tui-unified: one desktop for both languages - it reads
     * shell.lang at start, every label is per-language and F2 switches
     * (and persists) the language at runtime. */
    return tui_desktop_main();
}

int shell_cmd_tui_test(const char *args) {
    int failed = tui_test_run(args && args[0] == 'v');
    return failed;                   /* exit code = failure count */
}

int shell_cmd_tui_hw(const char *args) {
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

int shell_cmd_tui_net(const char *args) {
    (void)args;
    tui_init();
    int rc = tui_net_app();
    tui_shutdown();
    return rc;
}

int shell_cmd_tui_sysinfo(const char *args) {
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

int shell_cmd_tui_time(const char *args) {
    (void)args;
    tui_init();
    int rc = tui_time_main();
    tui_shutdown();
    return rc;
}

int shell_cmd_tui_drv(const char *args) {
    if (args && args[0]) return shell_cmd_tui_hw(args);  /* alias: tui-drv PCI == tui-hw PCI */
    tui_init();
    int rc = tui_hw_app();
    tui_shutdown();
    return rc;
}

int shell_cmd_tui_sys(const char *args) {
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
int shell_cmd_tui_ext(const char *args) {
    tui_init();
    tui_ext_page_main("ext");
    (void)args;
    tui_shutdown();
    return 0;
}


