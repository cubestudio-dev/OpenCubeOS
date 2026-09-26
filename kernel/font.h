/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/font.h
 * Purpose: Text renderer API + font engine extension point.
 *
 * L0 ships a built-in 8x16 ASCII bitmap font. L1 extensions may register
 * an alternative font engine (see ext.h - oc_ext_register_font_engine).
 *
 * The renderer is intentionally simple: one glyph is blitted at a time
 * using oc_fb_* primitives. A simple dirty-cell optimization is left for
 * a later WP.
 */
#ifndef OC_FONT_H
#define OC_FONT_H

#include "types.h"
#include "fb.h"

#define OC_FONT_CELL_W 8
#define OC_FONT_CELL_H 16

/* Built-in glyph table. */
extern const u8 oc_font8x16[256][16];

/* Renderer ops - pluggable so L1 can swap renderers (see ext.h). */
typedef struct oc_renderer {
    /* Draw a single code point `cp` at pixel (x, y) with foreground and
     * background color packed as framebuffer-native u32 pixels. */
    void (*draw_glyph)(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel);
    /* Width/height of one cell, in pixels. */
    u32   cell_w;
    u32   cell_h;
    const char* name;
} oc_renderer_t;

/* Default renderer: uses oc_font8x16, 8x16 cell, fb primitives. */
void oc_renderer_default_draw_glyph(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel);

/* Active renderer (starts as default). */
extern oc_renderer_t oc_g_default_renderer;
extern oc_renderer_t* oc_g_active_renderer;

/* Set the active renderer (returns the previous one). */
oc_renderer_t* oc_renderer_set_active(oc_renderer_t* r);

#endif /* OC_FONT_H */
