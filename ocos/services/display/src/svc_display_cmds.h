/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display + services/input op
 * surface (exported bodies; see disp_lang.h for the lang op).
 * 3.0.0-WP03 provenance: stats/uname/clear rescued from core/main.c,
 * lang from recovery/shell_lang.c, the CJK six from
 * chinese/chinese_cmd.c (all renamed disp_cmd_*), touchpad/touchtest
 * from recovery/shell_cmds_chipset.c.  The IME pair (ime / ime-test)
 * left in WP-03 and is revived in 3.0.0-WP04 G11b (bodies back in
 * disp_cmds_cn.c, engine in ime_engine.c). */
#ifndef OC_DISP_INP_CMDS_H
#define OC_DISP_INP_CMDS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* services/display/disp_cmds_kern.c (rescued from core/main.c) */
int disp_cmd_stats(const char *args);
int disp_cmd_uname(const char *args);
int disp_cmd_clear(const char *args);
/* services/display/disp_lang.c (lang op) */
int disp_cmd_lang(const char *args);
/* services/display/disp_cmds_cn.c (CJK six, rescued from chinese_cmd.c) */
int disp_cmd_chinese(const char *args);
int disp_cmd_chinesetest(const char *args);
int disp_cmd_font(const char *args);
int disp_cmd_font_test(const char *args);
int disp_cmd_encoding(const char *args);
int disp_cmd_encoding_test(const char *args);
/* 3.0.0-WP04 G11b: revived IME pair (bodies in disp_cmds_cn.c) */
int disp_cmd_ime(const char *args);
int disp_cmd_ime_test(const char *args);
/* interactive console compose line (disp_cmds_cn.c; the TUI pad in
 * the G11c tui service has its own compose inside tui_chinese_input,
 * but tui_cn_hw's console fallback and any future caller reuse this) */
int ime_console_compose(u8 *out, int cap);

/* services/input/inp_cmds.c (touchpad family, rescued from
 * recovery/shell_cmds_chipset.c) */
int inp_cmd_touchpad(const char *args);
int inp_cmd_touchtest(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_DISP_INP_CMDS_H */
