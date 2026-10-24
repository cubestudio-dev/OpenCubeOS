/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/display + services/input command
 * surface (exported bodies, migrated by declaration). */
#ifndef OC_DISP_INP_CMDS_H
#define OC_DISP_INP_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* core/main.c */
int shell_cmd_stats(const char *args);
int shell_cmd_uname(const char *args);
int shell_cmd_clear(const char *args);
/* recovery/shell_lang.c */
int shell_cmd_lang(const char *args);
/* chinese/chinese_cmd.c (locale family) */
int disp_cmd_chinese(const char *args);
int disp_cmd_chinesetest(const char *args);
int disp_cmd_font(const char *args);
int disp_cmd_font_test(const char *args);
int disp_cmd_encoding(const char *args);
int disp_cmd_encoding_test(const char *args);
int disp_cmd_ime(const char *args);
int disp_cmd_ime_test(const char *args);

/* recovery/shell_cmds_chipset.c (input family) */
int inp_cmd_touchpad(const char *args);
int inp_cmd_touchtest(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_DISP_INP_CMDS_H */
