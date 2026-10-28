/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display language mechanism
 * (bodies rescued verbatim from recovery/shell_lang.c; see
 * disp_lang.h for provenance). */
#include "disp_lang.h"
#include "lib_config.h"
#include "lib_string.h"
#include "screen_console.h"
#include "console_cn.h"

static int g_disp_lang = DISP_LANG_EN;

int disp_lang_get(void) {
    return g_disp_lang;
}

void disp_lang_set(int lang) {
    g_disp_lang = (lang == DISP_LANG_ZH) ? DISP_LANG_ZH : DISP_LANG_EN;
}

void disp_lang_set_str(const char *s) {
    if (s && s[0] == 'z' && s[1] == 'h' && (s[2] == 0 || s[2] == ' ')) {
        g_disp_lang = DISP_LANG_ZH;
        return;
    }
    if (s && s[0] == 'e' && s[1] == 'n' && (s[2] == 0 || s[2] == ' ')) {
        g_disp_lang = DISP_LANG_EN;
        return;
    }
    g_disp_lang = DISP_LANG_EN;
}

const char *disp_lang_name(void) {
    return (g_disp_lang == DISP_LANG_ZH) ? "zh" : "en";
}

/* ---- strings the display lang op itself emits -----------------------
 * (the bilingual shell-UI message table died with the oc> shell; these
 * five are the op surface's own output, rescued byte-identical from
 * the historical SM_LANG_* / SM_PERSIST_* table entries - the zh
 * column is the CJK rendering path this very op selects). */
static const char *disp_lang_msg_cur(int zh) {
    return zh ? "Console output language: "
              : "shell output language: ";
}
static const char *disp_lang_msg_set(int zh) {
    return zh ? "Console output language set to "
              : "shell output language set to ";
}
static const char *disp_lang_msg_usage(int zh) {
    return zh ? "Usage: lang [en|zh] (no argument shows the current language)"
              : "usage: lang [en|zh]  (no argument shows the current language)";
}
static const char *disp_lang_msg_persist_ok(int zh) {
    (void)zh;
    return " (persisted)";
}
static const char *disp_lang_msg_persist_ram(int zh) {
    (void)zh;
    return " (in-memory only; ramfs config, lost on reboot)";
}

int disp_cmd_lang(const char *args) {
    int zh = (g_disp_lang == DISP_LANG_ZH);
    if (!args || !args[0]) {
        console_cn_puts(disp_lang_msg_cur(zh));
        console_cn_puts(disp_lang_name());
        console_cn_putcw('\n');
        return 0;
    }
    /* trim the first token */
    char tok[8];
    int n = 0;
    const char *p = args;
    while (*p == ' ') p++;
    while (p[n] && p[n] != ' ' && n < 7) { tok[n] = p[n]; n++; }
    tok[n] = 0;
    if ((tok[0] == 'e' && tok[1] == 'n' && tok[2] == 0) ||
        (tok[0] == 'z' && tok[1] == 'h' && tok[2] == 0)) {
        disp_lang_set_str(tok);
        int wr = lib_config_write("shell.lang", tok);
        console_cn_puts(disp_lang_msg_set(zh));
        console_cn_puts(tok);
        if (wr == 0) {
            /* honest persistence report - the /etc volume can be
             * FAT32 (edits survive) or the ramfs fallback (lost on
             * reboot); rc==0 alone must not claim "persisted". */
            console_cn_puts(lib_config_backend_fat() == 1
                                ? disp_lang_msg_persist_ok(zh)
                                : disp_lang_msg_persist_ram(zh));
        } else {
            console_cn_puts(" (in-memory only; config write rc=");
            char num[12];
            u64_to_str((u64)(wr < 0 ? -wr : wr), num);
            console_cn_puts(num);
            console_cn_puts(")");
        }
        console_cn_putcw('\n');
        return 0;
    }
    console_cn_puts(disp_lang_msg_usage(zh));
    console_cn_putcw('\n');
    return 1;
}
