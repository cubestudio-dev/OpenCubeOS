/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - shared L1 i18n runtime.
 *
 * Header-only (the _shared style): every L1 app compiles its own
 * copy.  The PER-APP string tables live in the generated
 * i18n_gen.c (built by tools/gen_userprogs.py from the app's
 * i18n/en.json + i18n/zh.json - flat legacy programs read theirs
 * from src/L1/_i18n/<name>/).  Nothing is hand-written in C: the
 * app sources say tr_("key") and the resource files carry the text.
 *
 * SELF-CONTAINED by design: the locale probe carries its own inline
 * int-0x80 sequence and the compare its own loop, so this header
 * never depends on _shared/l1_lib.h's wrapper order - l1_lib.h
 * includes it at its tail and every app gets tr_() for free.
 *
 * Lookup: i18n_lang_zh() asks the SYSTEM locale once per process
 * (SYS_L1SVC display/LANG_GET), then tr_() binary-searches the
 * selected table (tables are emitted key-sorted); a miss in zh
 * falls back to en, a miss in en falls back to the key itself -
 * an untranslated app can never crash or print garbage, it just
 * shows the key (or the English text).
 *
 * The reserved key "_hint" is the registry hint (help -a / the
 * manual pages), consumed by gen_userprogs.py at build time. */
#ifndef OC_L1_I18N_H
#define OC_L1_I18N_H

/* Only the numbering contract (SYS_L1SVC + the display op numbers). */
#include "_shared/l1svc_ops.h"

#ifndef SYS_L1SVC
#define SYS_L1SVC 110
#endif

/* ---- per-app generated tables (i18n_gen.c, linked into the app) -- */
struct i18n_kv {
    const char *k;
    const char *v;
};
extern const struct i18n_kv I18N_EN[];
extern const int            I18N_EN_N;
extern const struct i18n_kv I18N_ZH[];
extern const int            I18N_ZH_N;

/* ---- locale -------------------------------------------------------
 * One SYS_L1SVC round trip per process (cached in a static).  Any
 * failure (old kernel, service down) reads as English - the safe
 * default.  The int-0x80 sequence is the l1_lib.h syscall5 contract
 * spelled locally: SYS_L1SVC(110), cat=DISPLAY, op=LANG_GET,
 * arg=0, out=&buf, cap=sizeof(buf). */
static inline int i18n_lang_zh(void) {
    static int cached = -1;
    if (cached < 0) {
        char out[8];
        out[0] = 0;
        register long r10 __asm__("r10") = (long)out;
        register long r8  __asm__("r8")  = (long)sizeof(out);
        long rc;
        __asm__ volatile("int $0x80"
                         : "=a"(rc)
                         : "a"((long)SYS_L1SVC),
                           "D"((long)L1SVC_CAT_DISPLAY),
                           "S"((long)L1SVC_DISP_LANG_GET),
                           "d"(0L), "r"(r10), "r"(r8)
                         : "memory");
        cached = (rc == 0 && out[0] == 'z' && out[1] == 'h') ? 1 : 0;
    }
    return cached;
}

/* Human name of the active locale (for status lines). */
static inline const char *i18n_lang_name(void) {
    return i18n_lang_zh() ? "zh" : "en";
}

/* ---- table lookup (binary search, tables are key-sorted) ---------- */
static inline const char *i18n_lookup(const struct i18n_kv *tab, int n,
                                      const char *key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *a = key, *b = tab[mid].k;
        while (*a && *a == *b) { a++; b++; }
        int c = (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (c == 0) return tab[mid].v;
        if (c < 0)  hi = mid - 1;
        else        lo = mid + 1;
    }
    return (const char *)0;
}

/* ---- the translator ------------------------------------------------
 * tr_("key"): zh table when the locale is zh (miss -> en table),
 * en table otherwise (miss -> the key itself, so a missing entry
 * is VISIBLE as the key string, never a crash).  An empty value
 * counts as missing. */
static inline const char *tr_(const char *key) {
    if (!key || !key[0]) return "";
    if (i18n_lang_zh()) {
        const char *v = i18n_lookup(I18N_ZH, I18N_ZH_N, key);
        if (v && v[0]) return v;
    }
    const char *v = i18n_lookup(I18N_EN, I18N_EN_N, key);
    if (v && v[0]) return v;
    return key;
}

#endif /* OC_L1_I18N_H */
