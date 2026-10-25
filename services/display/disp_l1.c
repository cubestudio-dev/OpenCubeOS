/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/display L1 surface (SYS_L1SVC cat
 * "display"): the rendering/locale family.  The full-screen TUI and
 * the chipset diagnostics stay in L0 (they drive kernel structures
 * directly / are recovery-env tooling). */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "disp_cmds.h"
#include "shell.h"
#include "config_gen.h"   /* OC_CONFIG_CHINESE for the locale ops */

typedef int (*disp_cmd_fn_t)(const char *);

static int disp_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    disp_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_DISP_STATS:       fn = shell_cmd_stats; break;
        case L1SVC_DISP_UNAME:       fn = shell_cmd_uname; break;
        case L1SVC_DISP_CLEAR:       fn = shell_cmd_clear; break;
        case L1SVC_DISP_LANG:        fn = shell_cmd_lang; break;
#if OC_CONFIG_CHINESE
        /* The locale command bodies live in chinese/chinese_cmd.c, which
         * is NOT compiled when CONFIG_CHINESE=n (CONFIG_TUI=n does not
         * touch them).  2.1.0 fix: without this guard a CONFIG_TUI=n
         * CONFIG_CHINESE=n image failed to LINK - the documented
         * off-path must build.  The op numbers stay stable (never
         * renumbered); with the tree off, the default arm below returns
         * a clean -1 to the ring-3 caller instead. */
        case L1SVC_DISP_CHINESE:     fn = disp_cmd_chinese; break;
        case L1SVC_DISP_CHINESETEST: fn = disp_cmd_chinesetest; break;
        case L1SVC_DISP_FONT:        fn = disp_cmd_font; break;
        case L1SVC_DISP_FONT_TEST:   fn = disp_cmd_font_test; break;
        case L1SVC_DISP_ENCODING:    fn = disp_cmd_encoding; break;
        case L1SVC_DISP_ENC_TEST:    fn = disp_cmd_encoding_test; break;
        case L1SVC_DISP_IME:         fn = disp_cmd_ime; break;
        case L1SVC_DISP_IME_TEST:    fn = disp_cmd_ime_test; break;
#endif
        default: return -1;
    }
    return shell_run_fn_captured(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: display service identity (console/font/locale family);
 * console bring-up stays in kmain. */
OC_SERVICE_DECL_L1("display", "display service (console + fonts + locale + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   disp_l1_dispatch);
