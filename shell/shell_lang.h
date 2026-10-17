/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support-fix3
 * File: shell/shell_lang.h
 * Purpose: user-configurable shell output language (shell.lang=en|zh).
 *
 * The default is English (en) -- system strings stay ASCII by design
 * (EXTENSIONS WP-10-chinese_support section 7).  When the user selects
 * zh (via `lang zh` or /etc/opencube.conf), the SYSTEM-LEVEL shell
 * messages (banner, help headers, common errors) switch to Chinese.
 * Per-command help texts and command-specific output stay English;
 * the full Chinese experience is the tui-cn launcher and its CN pages
 * (chinese/ module), which `tui` now opens directly when shell.lang=zh.
 */
#ifndef OC_SHELL_LANG_H
#define OC_SHELL_LANG_H

#define SHELL_LANG_EN 0
#define SHELL_LANG_ZH 1

/* current in-memory language (SHELL_LANG_EN or SHELL_LANG_ZH) */
int shell_lang_get(void);
void shell_lang_set(int lang);
/* set from a config string ("en"/"zh", case-insensitive, default en) */
void shell_lang_set_str(const char *s);
/* current language as a config string ("en"/"zh") */
const char *shell_lang_name(void);

/* message ids for the bilingual table */
enum shell_msg_id {
    SM_READY = 0,      /* version argument appended by the caller */
    SM_TRY,
    SM_HELP_AZ,
    SM_HELP_AZ_TAIL,   /* number printed by the caller before this */
    SM_HELP_WP,
    SM_TOTAL,
    SM_OTHER_BUCKET,
    SM_UNKNOWN_PRE,    /* "unknown command: "          */
    SM_UNKNOWN_POST,   /* " (try 'help')"              */
    SM_LANG_CUR,       /* %s = en|zh                   */
    SM_LANG_SET,       /* %s = en|zh                   */
    SM_LANG_USAGE,
    SM_CFG_SAVED,
    SM_COUNT
};

/* localized system message (never NULL; unknown ids return the English
 * empty string so a bad id can never crash the shell) */
const char *shell_msg(int id);

/* the `lang` shell command (registered in kmain):
 * lang           -> print the current language
 * lang en|zh     -> switch AND persist to /etc/opencube.conf */
int shell_cmd_lang(const char *args);

#endif /* OC_SHELL_LANG_H */
