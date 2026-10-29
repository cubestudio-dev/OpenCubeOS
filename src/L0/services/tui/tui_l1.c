/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 G11c - services/tui L1 surface (SYS_L1SVC
 * cat "tui"): the full-screen desktop family, revived verbatim from
 * v2.2.0 tui/tui_cmd.c.  Eight of the nine ops are full-screen
 * INTERACTIVE pages (framebuffer rendering + PS/2 keyboard until the
 * user leaves); they route DIRECT, not through the core_kcap capture -
 * their console writes are live session output (toast lines, terminal
 * handoffs, on-exit summaries) that must reach the screen/serial in
 * real time, the same live-output rule the G11a sshd op and the G11b
 * "ime -i" op established.  The exception is op 2 (tui-test): the
 * self-test suite prints its PASS/FAIL verdict block through the
 * plain console, so the captured path serves it exactly.
 *
 * The command bodies keep their v2.2.0 shell_cmd_tui_* names and
 * shapes (tui_cmd.c); the op ABI is the category-number folder rule:
 * category "tui" = services/tui. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "tui.h"
#include "tui_ext.h"
#include "core_kcap.h"

/* The nine v2.2.0 command bodies (tui_cmd.c; static there, exported
 * for this dispatch in the G11c port). */
int shell_cmd_tui(const char *args);
int shell_cmd_tui_test(const char *args);
int shell_cmd_tui_hw(const char *args);
int shell_cmd_tui_net(const char *args);
int shell_cmd_tui_sysinfo(const char *args);
int shell_cmd_tui_time(const char *args);
int shell_cmd_tui_drv(const char *args);
int shell_cmd_tui_sys(const char *args);
int shell_cmd_tui_ext(const char *args);

static int tui_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    /* All nine ops run DIRECT: live rendering, live keyboard.  out
     * stays empty; each page's own console output is the session (the
     * serial tee mirrors it verbatim).  G11c field note: tui-test was
     * first routed through the core_kcap capture, but its verdict
     * block is ~4.4 KiB - the 4096-byte SVC result buffer truncated
     * the final summary.  v2.2.0 streamed the block live to the
     * console; the direct route restores exactly that (and the
     * framebuffer self-tests still draw on the fb regardless). */
    switch (op) {
        case L1SVC_TUI_TUI:      out[0] = 0; return shell_cmd_tui(arg ? arg : "");
        case L1SVC_TUI_TEST:     out[0] = 0; return shell_cmd_tui_test(arg ? arg : "");
        case L1SVC_TUI_HW:       out[0] = 0; return shell_cmd_tui_hw(arg ? arg : "");
        case L1SVC_TUI_NET:      out[0] = 0; return shell_cmd_tui_net(arg ? arg : "");
        case L1SVC_TUI_SYSINFO:  out[0] = 0; return shell_cmd_tui_sysinfo(arg ? arg : "");
        case L1SVC_TUI_TIME:     out[0] = 0; return shell_cmd_tui_time(arg ? arg : "");
        case L1SVC_TUI_DRV:      out[0] = 0; return shell_cmd_tui_drv(arg ? arg : "");
        case L1SVC_TUI_SYS:      out[0] = 0; return shell_cmd_tui_sys(arg ? arg : "");
        case L1SVC_TUI_EXT:      out[0] = 0; return shell_cmd_tui_ext(arg ? arg : "");
        default: return -1;
    }
}

/* 3.0.0-WP04 G11c: the builtin themes + hotkeys moved from the v2.2.0
 * kmain call (tui_ext_init() ran there behind OC_CONFIG_TUI) into the
 * service init slot - the service registry brings every service's init
 * up at boot, so the theme/hotkey registries are warm before the first
 * op (tui_init() itself stays the page-registration entry). */
OC_SERVICE_DECL_L1("tui", "tui service (full-screen desktop + classic pages + ring-3 surface)",
                   (oc_service_init_fn_t)tui_ext_init, (oc_service_late_fn_t)0,
                   tui_l1_dispatch);
