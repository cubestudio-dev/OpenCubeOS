/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display: console output language.
 *
 * Verbatim rescue of the language mechanism out of the oc> shell
 * (recovery/shell_lang.c).  The state is a DISPLAY-pipeline setting:
 * it selects which column of the CJK-capable console message strings
 * is served, and the persisted preference key stays "shell.lang" in
 * /etc/opencube.conf (kept for data compatibility with existing
 * installs).  The old bilingual SHELL-UI message table (help headers,
 * prompt strings, unknown-command banners) died with the shell; only
 * the strings the L1SVC "display" op surface itself emits survive,
 * as file-local constants.
 */
#ifndef OC_DISP_LANG_H
#define OC_DISP_LANG_H

#ifdef __cplusplus
extern "C" {
#endif

/* The DISP_LANG_* values + the disp_lang_get() contract live in the
 * kernel-side core_lib_i18n.h (the NAK dependency direction: the
 * display service implements, the kernel lib declares). */
#include "core_lib_i18n.h"
void disp_lang_set(int lang);
/* Set from a config string ("en"/"zh"; anything else = en). */
void disp_lang_set_str(const char *s);
/* Current language as a config string ("en"/"zh"). */
const char *disp_lang_name(void);

/* L1SVC display op: L1SVC_DISP_LANG - print / switch / persist the
 * console output language (no argument = print; "en"|"zh" = set). */
int disp_cmd_lang(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_DISP_LANG_H */
