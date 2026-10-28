/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - core/kcap implementation.
 *
 * Verbatim rescue of the WP-05 capture stack from recovery/shell.c
 * (the file died with the oc> shell; this mechanism could not - it is
 * how every L1SVC op body's output reaches ring 3).  The fix history
 * carried over: BUG-0083 (capture STACK instead of one global buffer),
 * BUG-016 (no serial leak while capturing), BUG-025 (overflow marked,
 * never silent), BUG-0091 (suppression contract honored by the
 * driver-side hook slot).
 *
 * 3.0.0 structural change: the console hook slot is driver-owned
 * (drivers/display/screen_console.c), so install/uninstall below goes
 * through screen_console_set_hook() instead of the old
 * l1_ext_set_console_hook() detour through services/.
 */
#include "core_kcap.h"
#include "screen_console.h"
#include "mem_heap.h"
#include "lib_string.h"

typedef struct {
    char *buf;
    int   size;
    int   cap;
    int   active;
    int   overflowed;
} kcap_capture_t;

#define OC_KCAP_MAX 4
static kcap_capture_t g_kcap_stk[OC_KCAP_MAX];
static int            g_kcap_depth = 0;
#define g_kcap (g_kcap_stk[g_kcap_depth > 0 ? g_kcap_depth - 1 : 0])

/* The kernel's base hook (the boot serial tee).  While capturing, the
 * capture hook replaces it in the console driver's single slot; the
 * base hook is restored when the last capture goes away. */
static core_kcap_hook_fn_t g_base_hook     = NULL;
static void               *g_base_hook_ctx = NULL;
static int                 g_installed    = 0;

/* The capturing hook: walk down to the innermost ACTIVE capture.
 * Output produced while the top slot is frozen (between end and free)
 * belongs to the next outer active capture - a nested capture keeps
 * collecting.  With no active capture at all the char is forwarded to
 * the base hook exactly as before (that state exists only transiently
 * during install/uninstall juggling).  Returns 1 = suppressed (the
 * console must not draw), so neither framebuffer nor serial see the
 * captured chars. */
static int kcap_hook(void *ctx, u8 ch) {
    (void)ctx;
    for (int d = g_kcap_depth - 1; d >= 0; d--) {
        kcap_capture_t *c = &g_kcap_stk[d];
        if (!c->active || !c->buf) continue;
        if (c->size + 1 < c->cap) {
            c->buf[c->size++] = (char)ch;
            c->buf[c->size] = 0;
        } else {
            c->overflowed = 1;   /* marked, never silently dropped */
        }
        return 1;   /* suppress framebuffer AND serial during capture */
    }
    if (g_base_hook) g_base_hook(g_base_hook_ctx, ch);
    return 0;
}

void core_kcap_set_base_hook(core_kcap_hook_fn_t fn, void *ctx) {
    g_base_hook     = fn;
    g_base_hook_ctx = ctx;
    if (!g_installed) {
        screen_console_set_hook((screen_console_hook_fn)kcap_hook, NULL);
        g_installed = 1;
    }
    /* If we're currently capturing, the capture hook remains the
     * installed one (it forwards to the base hook when idle). */
}

static void kcap_install(void) {
    if (g_installed) return;
    screen_console_set_hook((screen_console_hook_fn)kcap_hook, NULL);
    g_installed = 1;
}

static void kcap_uninstall(void) {
    if (!g_installed) return;
    screen_console_set_hook((screen_console_hook_fn)g_base_hook, g_base_hook_ctx);
    g_installed = 0;
}

static int kcap_begin(int cap) {
    if (g_kcap_depth >= OC_KCAP_MAX) return -1;   /* stack full */
    if (cap <= 0) cap = 8192;
    char *buf = (char *)kmalloc((u64)cap);
    if (!buf) return -1;
    g_kcap_depth++;   /* push: g_kcap now aliases the new slot */
    g_kcap.buf = buf;
    g_kcap.size = 0;
    g_kcap.cap = cap;
    g_kcap.overflowed = 0;
    g_kcap.buf[0] = 0;
    g_kcap.active = 1;
    kcap_install();
    return 0;
}

static void kcap_end(void) {
    /* Mark only the TOP capture finished; the buffer stays readable
     * until kcap_free pops it. */
    if (g_kcap_depth <= 0 || !g_kcap.active) return;
    g_kcap.active = 0;
}

static void kcap_free(void) {
    if (g_kcap_depth <= 0) return;
    if (g_kcap.buf) {
        kfree(g_kcap.buf);
        g_kcap.buf = NULL;
    }
    g_kcap.size = 0;
    g_kcap.cap = 0;
    g_kcap.active = 0;
    g_kcap_depth--;
    if (g_kcap_depth == 0) kcap_uninstall();
}

int core_kcap_capturing(void) {
    return g_kcap_depth > 0;
}

int core_kcap_run_fn(int (*fn)(const char *), const char *arg,
                     char *out, int out_cap) {
    if (!fn || !out || out_cap <= 0) return -1;
    out[0] = 0;
    if (kcap_begin(out_cap) != 0) return -1;
    int rc = fn(arg ? arg : "");
    kcap_end();
    int n = g_kcap.size;
    if (n > out_cap - 1) n = out_cap - 1;
    if (n > 0 && g_kcap.buf) {
        memcpy(out, g_kcap.buf, (usize)n);
    }
    out[n > 0 ? n : 0] = 0;
    kcap_free();
    (void)rc;
    return 0;
}
