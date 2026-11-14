/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - the L0-side i18n runtime.
 *
 * The lookup logic for l0_tr_() (see l0_i18n.h for the contract and
 * the resource chain).  The locale selector is the display service's
 * disp_lang_get() - the very `lang` command this batch makes real. */
#include "core_lib_i18n.h"
#include "core_lib_string.h"
#include "svc_display_lang.h"

static const struct l0_i18n_kv *lookup(const struct l0_i18n_kv *tab,
                                       int n, const char *key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(tab[mid].k, key);
        if (c == 0) return &tab[mid];
        if (c < 0) lo = mid + 1;
        else       hi = mid - 1;
    }
    return (const struct l0_i18n_kv *)0;
}

const char *l0_tr_(const char *key) {
    if (!key || !key[0]) return "";
    if (disp_lang_get() == DISP_LANG_ZH) {
        const struct l0_i18n_kv *e = lookup(L0_I18N_ZH, L0_I18N_ZH_N, key);
        if (e && e->v[0]) return e->v;
    }
    const struct l0_i18n_kv *e = lookup(L0_I18N_EN, L0_I18N_EN_N, key);
    if (e && e->v[0]) return e->v;
    return key;
}

const char *l0_i18n_lang_name(void) {
    return (disp_lang_get() == DISP_LANG_ZH) ? "zh" : "en";
}
