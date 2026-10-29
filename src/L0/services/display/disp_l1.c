/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display L1 surface (SYS_L1SVC cat
 * "display"): the rendering/locale family.  3.0.0-WP03: the full-screen
 * TUI died with the user plane; the CJK capability files (font /
 * encoding / render / console_cn) are always compiled in (the
 * CONFIG_TUI / CONFIG_CHINESE knobs are gone); the IME op pair
 * (L1SVC_DISP_IME / L1SVC_DISP_IME_TEST) left in WP-03 with the
 * input-method engine.  3.0.0-WP04 G11b revives the pair (engine in
 * ime_engine.c, bodies in disp_cmds_cn.c; op ABI unchanged). */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "disp_cmds.h"
#include "disp_lang.h"
#include "core_kcap.h"

typedef int (*disp_cmd_fn_t)(const char *);

static int disp_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    if (op == L1SVC_DISP_IME && arg && arg[0] == '-' && arg[1] == 'i') {
        /* 3.0.0-WP04 G11b: "ime -i" is the INTERACTIVE compose line -
         * its live '\r' redraws (pending pinyin + candidate strip)
         * must reach the console/serial in real time, exactly as in
         * v2.2.0, and its keystrokes come from the PS/2 keyboard
         * queue.  Route it DIRECTLY, not through the core_kcap capture
         * (which would swallow the redraw stream until the syscall
         * returns) - the same live-output rule the G11a sshd op uses.
         * The op text stays empty; the compose loop's own console
         * output is the transcript.  The plain status form ("ime"
         * without -i) stays on the captured path below. */
        out[0] = 0;
        return disp_cmd_ime(arg);
    }
    disp_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_DISP_STATS:       fn = disp_cmd_stats; break;
        case L1SVC_DISP_UNAME:       fn = disp_cmd_uname; break;
        case L1SVC_DISP_CLEAR:       fn = disp_cmd_clear; break;
        case L1SVC_DISP_LANG:        fn = disp_cmd_lang; break;
        case L1SVC_DISP_CHINESE:     fn = disp_cmd_chinese; break;
        case L1SVC_DISP_CHINESETEST: fn = disp_cmd_chinesetest; break;
        case L1SVC_DISP_FONT:        fn = disp_cmd_font; break;
        case L1SVC_DISP_FONT_TEST:   fn = disp_cmd_font_test; break;
        case L1SVC_DISP_ENCODING:    fn = disp_cmd_encoding; break;
        case L1SVC_DISP_ENC_TEST:    fn = disp_cmd_encoding_test; break;
        case L1SVC_DISP_IME:         fn = disp_cmd_ime; break;
        case L1SVC_DISP_IME_TEST:    fn = disp_cmd_ime_test; break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: display service identity (console/font/locale family);
 * console bring-up stays in kmain. */
OC_SERVICE_DECL_L1("display", "display service (console + fonts + locale + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   disp_l1_dispatch);
