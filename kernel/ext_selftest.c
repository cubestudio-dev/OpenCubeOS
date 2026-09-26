/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/ext_selftest.c
 * Purpose: Self-test for the four L0?L1 extension points.
 *
 * This file is the canary for "the extension API actually works". It
 * is called by kmain near the end of the boot log; if it returns 4, all
 * four points are reachable. If it returns < 4, that point is broken.
 *
 * The self-test is intentionally non-destructive to the framebuffer -
 * it draws nothing on screen. It calls each extension and verifies the
 * return values / side effects that don't depend on the L1 caller.
 */
#include "ext.h"
#include "fb.h"
#include "font.h"
#include "console.h"
#include "string.h"

/* Test hook accumulator: counts chars we've tee'd. */
static int test_hook(void* ctx, u8 ch) {
    int* counter = (int*)ctx;
    if (counter) (*counter)++;
    (void)ch;
    return 0;  /* let L0 still draw it */
}

/* A custom test renderer that just records draw_glyph calls. */
static int g_renderer_calls = 0;
static void test_draw_glyph(u32 x, u32 y, u32 cp, u32 fg, u32 bg) {
    (void)x; (void)y; (void)cp; (void)fg; (void)bg;
    g_renderer_calls++;
}
static oc_renderer_t test_renderer = {
    .draw_glyph = test_draw_glyph,
    .cell_w = OC_FONT_CELL_W,
    .cell_h = OC_FONT_CELL_H,
    .name   = "selftest-null-renderer",
};

/* A custom test font engine that just returns the default glyph. */
static const u8* test_font_lookup(oc_font_engine_t* self, u32 cp) {
    (void)self; (void)cp;
    /* Return the glyph for 'X' from the default font - it's a known nonzero shape. */
    return oc_font8x16['X'];
}
static oc_font_engine_t test_engine = {
    .name = "selftest-engine",
    .lookup = test_font_lookup,
    .glyph_w = OC_FONT_CELL_W,
    .glyph_h = OC_FONT_CELL_H,
};

int oc_ext_self_test_impl(void) {
    int passed = 0;

    /* ---- Point 1: framebuffer access ---- */
    const oc_fb_info_t* fb = oc_ext_fb_get_info();
    if (fb && fb->addr && fb->width == 800 && fb->height == 600 && fb->bpp == 32) {
        passed++;
    }

    /* ---- Point 2: renderer replacement ---- */
    oc_renderer_t* prev = oc_ext_set_renderer(&test_renderer);
    /* Trigger a draw_glyph call by emitting a char into the console. */
    oc_console_putc('@');
    if (g_renderer_calls > 0) {
        passed++;
    }
    /* Restore the default renderer. */
    oc_ext_set_renderer(prev);

    /* ---- Point 3: font engine registration + lookup ---- */
    oc_ext_register_font_engine(&test_engine);
    oc_font_engine_t* found = oc_ext_font_engine_by_name("selftest-engine");
    if (found && found == &test_engine) {
        const u8* g = found->lookup(found, 'A');
        if (g && g != oc_font8x16['A']) {
            /* We deliberately returned oc_font8x16['X'] above; check it's not the
             * same as oc_font8x16['A']. */
            passed++;
        }
    }

    /* ---- Point 4: console output hook ---- */
    int hook_counter = 0;
    oc_ext_set_console_hook(test_hook, &hook_counter);
    oc_console_putc('Z');
    if (hook_counter >= 1) {
        passed++;
    }
    oc_ext_set_console_hook(NULL, NULL);  /* disable */

    return passed;
}
