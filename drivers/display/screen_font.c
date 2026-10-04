/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/font.c
 */
#include "screen_font.h"
#include "screen_fb.h"

void screen_renderer_default_draw_glyph(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel) {
    const u8* glyph = screen_font8x16[cp & 0xFF];
    /* For each of 16 rows: for each of 8 columns, pick fg or bg pixel. */
    for (u32 row = 0; row < OC_FONT_CELL_H; ++row) {
        u8  bits   = glyph[row];
        u32 ypix   = y + row;
        u8* rowptr = screen_fb_get_info()->addr + (u64)ypix * screen_fb_get_info()->pitch;
        for (u32 col = 0; col < OC_FONT_CELL_W; ++col) {
            u32 xpix = x + col;
            if (xpix >= screen_fb_get_info()->width || ypix >= screen_fb_get_info()->height) continue;
            u32* px = (u32*)(rowptr + (u64)xpix * 4);
            *px = (bits & (0x80 >> col)) ? fg_pixel : bg_pixel;
        }
    }
}

screen_renderer_t screen_default_renderer = {
    .draw_glyph = screen_renderer_default_draw_glyph,
    .cell_w     = OC_FONT_CELL_W,
    .cell_h     = OC_FONT_CELL_H,
    .name       = "default-8x16-ascii",
};

screen_renderer_t* screen_active_renderer = &screen_default_renderer;

screen_renderer_t* screen_renderer_set_active(screen_renderer_t* r) {
    if (!r) return NULL;
    screen_renderer_t* prev = screen_active_renderer;
    screen_active_renderer = r;
    return prev;
}
