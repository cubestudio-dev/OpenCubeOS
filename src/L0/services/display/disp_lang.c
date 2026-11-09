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
#include "l0_i18n.h"

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
 * 3.0.0-WP06g2: the historical zh columns of these five were ENGLISH
 * PLACEHOLDERS (the v2.2.0 rescue note said "the zh column is the CJK
 * rendering path this very op selects" - but the carried-over text
 * was English, so `lang zh` printed English; the WP06g1 close-out
 * recorded it as the "lang placeholder note").  They now render
 * through the L0 i18n resource (etc/i18n/l0/{en,zh}.json) keyed
 * disp_lang_* - real Chinese under zh, byte-identical English under
 * en (the resource IS the en text now, no file-local tables left). */
static const char *disp_lang_msg_cur(int zh) {
    (void)zh;
    return l0_tr_("disp_lang_cur");
}
static const char *disp_lang_msg_set(int zh) {
    (void)zh;
    return l0_tr_("disp_lang_set");
}
static const char *disp_lang_msg_usage(int zh) {
    (void)zh;
    return l0_tr_("disp_lang_usage");
}
static const char *disp_lang_msg_persist_ok(int zh) {
    (void)zh;
    return l0_tr_("disp_lang_persist_ok");
}
static const char *disp_lang_msg_persist_ram(int zh) {
    (void)zh;
    return l0_tr_("disp_lang_persist_ram");
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
