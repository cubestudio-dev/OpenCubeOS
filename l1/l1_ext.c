/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/ext.c
 * Purpose: L1 extension API implementation. Default L0 wiring:
 *   - fb accessor returns &g_fb
 *   - renderer swap delegates to screen_renderer_set_active()
 *   - default font engine wraps screen_font8x16[]
 *   - console hook is a single (fn, ctx) pair, initially NULL
 */
#include "l1_ext.h"
#include "screen_fb.h"
#include "screen_font.h"
#include "screen_console.h"
#include "lib_string.h"

/* ---- 1. Framebuffer access ---- */
const screen_fb_info_t* l1_ext_fb_get_info(void) {
    return screen_fb_get_info();
}

/* ---- 2. Renderer replacement ---- */
screen_renderer_t* l1_ext_set_renderer(screen_renderer_t* new_renderer) {
    return screen_renderer_set_active(new_renderer);
}

/* ---- 3. Font engine registration ---- */
#define OC_MAX_FONT_ENGINES 8
static screen_font_engine_t* g_engines[OC_MAX_FONT_ENGINES];
static int g_engine_count = 0;

/* Default font engine: serves glyphs from the built-in screen_font8x16 table. */
static const u8* default_font_lookup(screen_font_engine_t* self, u32 cp) {
    (void)self;
    return screen_font8x16[cp & 0xFF];
}
static screen_font_engine_t g_default_engine = {
    .name     = "default-8x16-vga",
    .lookup   = default_font_lookup,
    .glyph_w  = OC_FONT_CELL_W,
    .glyph_h  = OC_FONT_CELL_H,
};

int l1_ext_register_font_engine(screen_font_engine_t* engine) {
    if (!engine || !engine->name || !engine->lookup) return -1;
    if (g_engine_count >= OC_MAX_FONT_ENGINES) return -2;
    /* De-dup: if name matches, replace. */
    for (int i = 0; i < g_engine_count; ++i) {
        if (g_engines[i] && strcmp(g_engines[i]->name, engine->name) == 0) {
            g_engines[i] = engine;
            return 0;
        }
    }
    g_engines[g_engine_count++] = engine;
    return 0;
}

screen_font_engine_t* l1_ext_font_engine_by_name(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_engine_count; ++i) {
        if (g_engines[i] && strcmp(g_engines[i]->name, name) == 0)
            return g_engines[i];
    }
    return NULL;
}

screen_font_engine_t* l1_ext_default_font_engine(void) {
    return &g_default_engine;
}

/* ---- 4. Console output hook ---- */
static screen_console_hook_fn g_hook_fn  = NULL;
static void*              g_hook_ctx = NULL;

void l1_ext_set_console_hook(screen_console_hook_fn hook, void* ctx) {
    g_hook_fn  = hook;
    g_hook_ctx = ctx;
}

void l1_ext_console_hook(u8 ch) {
    if (g_hook_fn) {
        g_hook_fn(g_hook_ctx, ch);
    }
}

/* ---- Self-test: invokes every extension point ---- */
/* Lives in ext_selftest.c (separate file so L1 can read the source and
 * see exactly which calls it must make). */
int l1_ext_self_test_impl(void);

int l1_ext_self_test(void) {
    return l1_ext_self_test_impl();
}

/* Note: the default font engine is registered explicitly by kmain at boot.
 * (Freestanding kernels don't run __attribute__((constructor)) ctors.) */
