/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-01
 * File: kernel/font.c
 */
#include "font.h"
#include "fb.h"

void oc_renderer_default_draw_glyph(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel) {
    const u8* glyph = oc_font8x16[cp & 0xFF];
    /* For each of 16 rows: for each of 8 columns, pick fg or bg pixel. */
    for (u32 row = 0; row < OC_FONT_CELL_H; ++row) {
        u8  bits   = glyph[row];
        u32 ypix   = y + row;
        u8* rowptr = oc_fb_get_info()->addr + (u64)ypix * oc_fb_get_info()->pitch;
        for (u32 col = 0; col < OC_FONT_CELL_W; ++col) {
            u32 xpix = x + col;
            if (xpix >= oc_fb_get_info()->width || ypix >= oc_fb_get_info()->height) continue;
            u32* px = (u32*)(rowptr + (u64)xpix * 4);
            *px = (bits & (0x80 >> col)) ? fg_pixel : bg_pixel;
        }
    }
}

oc_renderer_t oc_g_default_renderer = {
    .draw_glyph = oc_renderer_default_draw_glyph,
    .cell_w     = OC_FONT_CELL_W,
    .cell_h     = OC_FONT_CELL_H,
    .name       = "default-8x16-ascii",
};

oc_renderer_t* oc_g_active_renderer = &oc_g_default_renderer;

oc_renderer_t* oc_renderer_set_active(oc_renderer_t* r) {
    if (!r) return NULL;
    oc_renderer_t* prev = oc_g_active_renderer;
    oc_g_active_renderer = r;
    return prev;
}
