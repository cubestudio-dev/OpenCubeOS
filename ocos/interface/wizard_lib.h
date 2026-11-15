/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - shared L1 wizard library.
 *
 * Header-only (the _shared style): every L1 app compiles its own copy.
 * The eight merged apps (update / disk-tool / net-tool / sound-tool /
 * files / sysdiag / hwinfo / power) build their intent wizards on
 * these primitives.  The verbatim passthrough modes never come through
 * here - they keep the historical l1svc_main() semantics byte for
 * byte; only the interactive surfaces use the helpers below.
 *
 * Interaction contract (shared by all eight apps):
 *   - output buffer: the caller owns a WIZ_OUT_CAP static buffer
 *     (BSS, not stack - the 32 KiB l1svc_main note applies) and hands
 *     it to wiz_svc / wiz_svc_quiet;
 *   - line input: SYS_READLINE (kernel line editor: echo + backspace,
 *     blocking, printable ASCII only);
 *   - single-key picks: SYS_GETCH non-blocking poll (the ush console
 *     pattern), digits 1..9 select, q/Q/ESC aborts;
 *   - strings: every human-visible line renders through tr_() so the
 *     whole wizard follows `lang en|zh`; the shared keys wiz_quit /
 *     wiz_select / wiz_pause / wiz_svc_failed live in every merged
 *     app's i18n resource.
 *
 * Probing helpers (wiz_find / wiz_token_after) exist because the
 * wizard reads the SAME English service output a script would - the
 * service strings are the stable contract, the wizard never asks the
 * kernel for a second, "structured" spelling. */
#ifndef OC_L1_WIZARD_LIB_H
#define OC_L1_WIZARD_LIB_H

#include "l1_lib.h"
#include "l1svc_ops.h"

/* One L1SVC text result fits (help -a's full registry is ~12 KiB;
 * the biggest single op output stays well under this). */
#define WIZ_OUT_CAP 32768

/* ---- call wrappers ---------------------------------------------------
 * wiz_svc: run one L1SVC op, print its text, return the raw rc
 * (0 = ok).  On failure prints the shared tr_("wiz_svc_failed") line
 * so the user always sees WHY nothing happened. */
static inline long wiz_svc(long cat, long op, const char *arg, char *out) {
    long rc = sys_l1svc(cat, op, arg ? (long)arg : 0,
                        (long)out, (long)WIZ_OUT_CAP);
    if (rc != 0) {
        puts_(tr_("wiz_svc_failed"));
        return rc;
    }
    if (out[0]) {
        puts_(out);
        if (out[strlen_(out) - 1] != '\n') putc_('\n');
    }
    return 0;
}

/* wiz_svc_quiet: same call, prints nothing; the caller probes `out`
 * (the anchors are the service's own English output lines). */
static inline long wiz_svc_quiet(long cat, long op, const char *arg,
                                 char *out) {
    out[0] = 0;
    return sys_l1svc(cat, op, arg ? (long)arg : 0,
                     (long)out, (long)WIZ_OUT_CAP);
}

/* ---- output anchors --------------------------------------------------
 * wiz_find: plain substring probe (1 = present).  Written locally so
 * this header depends on nothing but l1_lib.h. */
static inline int wiz_find(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) return 0;
    for (const char *h = hay; *h; h++) {
        const char *a = h;
        const char *b = needle;
        while (*b && *a == *b) { a++; b++; }
        if (*b == 0) return 1;
    }
    return 0;
}

/* wiz_token_after: copy the first whitespace-delimited token that
 * follows `key` in `hay` into `dst` (cap bytes, always terminated).
 * "IP: 10.0.2.15" + "IP:" -> "10.0.2.15".  Returns dst when the key
 * was found (dst may be empty), NULL when the key itself is absent. */
static inline const char *wiz_token_after(const char *hay, const char *key,
                                          char *dst, int cap) {
    if (!hay || !key || !dst || cap < 1) return (const char *)0;
    dst[0] = 0;
    int klen = 0;
    while (key[klen]) klen++;
    for (const char *h = hay; *h; h++) {
        const char *a = h;
        const char *b = key;
        while (*b && *a == *b) { a++; b++; }
        if (*b != 0) continue;
        /* key matched at h; skip whitespace, take one token */
        const char *p = a;
        while (*p == ' ' || *p == '\t') p++;
        int n = 0;
        while (p[n] && p[n] != ' ' && p[n] != '\t' &&
               p[n] != '\n' && p[n] != '\r' && n < cap - 1) {
            dst[n] = p[n];
            n++;
        }
        dst[n] = 0;
        return dst;
    }
    return (const char *)0;
}

/* ---- input ----------------------------------------------------------- */
/* wiz_ask: prompt + one blocking line (SYS_READLINE: echo + backspace;
 * the kernel editor already echoes the closing newline, so no extra
 * one is printed here).  The buffer is always NUL-terminated; empty
 * input yields "". */
static inline void wiz_ask(const char *prompt, char *buf, int cap) {
    if (prompt && prompt[0]) {
        puts_(prompt);
        putc_(' ');
    }
    long n = syscall2(SYS_READLINE, (long)buf, (long)cap);
    if (n < 0) n = 0;
    if (n >= (long)cap) n = (long)cap - 1;
    buf[n] = 0;
}

/* wiz_confirm: one-shot y/N question.  ONLY a bare y / Y answers
 * yes - every other answer (empty, n, yes-with-typos) is NO.  The
 * destructive wizard paths (mkfs, abdisk, install, power off) layer
 * their retype-the-name confirmation on top of this. */
static inline int wiz_confirm(const char *what) {
    char ans[16];
    puts_(what);
    putc_(' ');
    long n = syscall2(SYS_READLINE, (long)ans, (long)sizeof(ans));
    if (n < 0) n = 0;
    ans[n] = 0;
    return (n == 1 && (ans[0] == 'y' || ans[0] == 'Y'));
}

/* wiz_pause: "press ENTER to continue" (used after long op output so
 * the menu redraw does not instantly scroll it away). */
static inline void wiz_pause(void) {
    char tmp[8];
    puts_(tr_("wiz_pause"));
    (void)syscall2(SYS_READLINE, (long)tmp, (long)sizeof(tmp));
}

/* wiz_menu: numeric single-key picker over tr_()-able item keys.
 * items[] holds i18n KEYS (rendered through tr_), count 1..9.  The
 * keys are consumed with the ush non-blocking getch pattern (spin
 * briefly between polls).  Returns the 1-based choice, or 0 on
 * q / Q / ESC.  Digits outside the range are ignored. */
static inline int wiz_menu(const char *title, const char *const *items,
                           int n) {
    if (n < 1) return 0;
    if (n > 9) n = 9;
    for (;;) {
        puts_("\n");
        puts_(title);
        putc_('\n');
        for (int i = 0; i < n; i++) {
            putc_(' ');
            putc_(' ');
            putu_((unsigned long)(i + 1));
            puts_(". ");
            puts_(tr_(items[i]));
            putc_('\n');
        }
        puts_("  q. ");
        puts_(tr_("wiz_quit"));
        putc_('\n');
        puts_(tr_("wiz_select"));
        putc_(' ');
        for (;;) {
            long k = sys_getch();
            if (k < 0) {
                for (volatile int s = 0; s < 400; s++) { /* brief spin */ }
                continue;
            }
            if (k >= '1' && k <= '9') {
                int c = (int)(k - '0');
                if (c <= n) {
                    putc_('\n');
                    return c;
                }
                continue;               /* digit beyond the list */
            }
            if (k == 'q' || k == 'Q' || k == 27) {
                putc_('\n');
                return 0;
            }
            /* any other key: redraw-safe ignore */
        }
    }
}

/* ---- banners / verdicts ---------------------------------------------- */
static inline void wiz_banner(const char *title) {
    puts_("\n=====");
    puts_(title);
    puts_("=====\n");
}

static inline void wiz_step(int step, int total, const char *what) {
    putc_('\n');
    puts_("[");
    putu_((unsigned long)step);
    putc_('/');
    putu_((unsigned long)total);
    puts_("] ");
    puts_(what);
    putc_('\n');
}

static inline void wiz_ok(const char *s)   { puts_("[OK] ");   puts_(s); putc_('\n'); }
static inline void wiz_fail(const char *s) { puts_("[!!] ");  puts_(s); putc_('\n'); }

#endif /* OC_L1_WIZARD_LIB_H */
