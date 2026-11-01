/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_i18n.h
 * Purpose: OCSE message catalog (en / zh). Messages are stored as
 * UTF-8; ocse_out() converts zh output to GB2312 on the fly so the
 * console_cn renderer picks the glyphs up; the GUI renders UTF-8
 * directly through the unicode font tables.
 */
#ifndef OCSE_I18N_H
#define OCSE_I18N_H

#define OCSE_LANG_EN 0
#define OCSE_LANG_ZH 1

/* Message keys. */
enum {
    OCSE_T_TITLE = 0,
    OCSE_T_SUBTITLE,
    OCSE_T_HW_HEAD,
    OCSE_T_CHECK_CPU,
    OCSE_T_CHECK_MEM,
    OCSE_T_CHECK_DISP,
    OCSE_T_CHECK_DISK,
    OCSE_T_PASS,
    OCSE_T_FAIL,
    OCSE_T_DISK_HEAD,
    OCSE_T_NO_DISK,
    OCSE_T_SPACE_LOW,
    OCSE_T_BTN_CLI,
    OCSE_T_BTN_LANG,
    OCSE_T_BTN_CHECK,
    OCSE_T_GUI_HINT,
    OCSE_T_CLI_BANNER,
    OCSE_T_CLI_HELP,
    OCSE_T_LANG_NOW,
    OCSE_T_DONE,
    OCSE_T_HALT,
    /* WP-06b: OCSECL installer strings */
    OCSE_T_REBOOTING,
    OCSE_T_POWEROFF,
    OCSE_T_POWEROFF_NOACPI,
    OCSE_T_P_NODISK,
    OCSE_T_P_NOPART,
    OCSE_T_P_BADPART,
    OCSE_T_P_NOTABLE,
    OCSE_T_P_NOFREE,
    OCSE_T_P_OVERLAP,
    OCSE_T_P_TABLEFULL,
    OCSE_T_P_CREATED,
    OCSE_T_P_REMOVED,
    OCSE_T_P_FLAGSET,
    OCSE_T_P_BADFLAG,
    OCSE_T_P_MNT_WARN,
    OCSE_T_P_BADRANGE,
    OCSE_T_COUNT_
};

void         ocse_i18n_init(void);
int          ocse_i18n_lang(void);
void         ocse_i18n_set(int lang);
const char*  ocse_tr(int key);       /* UTF-8 (zh) / ASCII (en) */
const char*  ocse_lang_name(int lang);/* "English" / "中文" */

/* Console+serial output line: converts zh to GB2312 for the console
 * renderer (the serial line gets the same bytes; QEMU log greps use
 * the en mode where everything is plain ASCII). */
void ocse_out(const char* s);
void ocse_outln(const char* s);

#endif /* OCSE_I18N_H */
