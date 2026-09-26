/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/console.c
 */
#include "console.h"
#include "font.h"
#include "fb.h"
#include "string.h"
#include "ext.h"   /* for the console output hook */

static oc_console_t g_console;

static void draw_cell(u32 cx, u32 cy, char ch, u32 fg_pixel, u32 bg_pixel) {
    if (!oc_g_active_renderer) return;
    u32 cw = oc_g_active_renderer->cell_w;
    u32 chh = oc_g_active_renderer->cell_h;
    /* P1-2 FIX: guard against divide-by-zero if renderer has 0 cell dims. */
    if (cw == 0 || chh == 0) return;
    u32 px = cx * cw;
    u32 py = cy * chh;
    oc_g_active_renderer->draw_glyph(px, py, (u8)ch, fg_pixel, bg_pixel);
}

static void scroll_up_one(void) {
    /* Shift the framebuffer rectangle up by one cell-row. We do this with
     * a row-by-row memcpy of the framebuffer's pixel data; this preserves
     * whatever was on screen (text + any graphics drawn by L1) and is
     * fast enough for boot log scrolling on an 800x600x32 surface. */
    u32 cw  = oc_g_active_renderer->cell_w;
    u32 chh = oc_g_active_renderer->cell_h;
    const oc_fb_info_t* fb = oc_fb_get_info();

    for (u32 cy = 0; cy + 1 < g_console.rows; ++cy) {
        u8* dst = fb->addr + (u64)(cy * chh) * fb->pitch;
        u8* src = fb->addr + (u64)((cy + 1) * chh) * fb->pitch;
        oc_memcpy(dst, src, (usize)fb->pitch * chh);
    }
    /* Clear the last cell-row to bg. */
    oc_fb_fill_rect(0, (g_console.rows - 1) * chh,
                    g_console.cols * cw, chh, g_console.bg_pixel);
}

int oc_console_init(void) {
    const oc_fb_info_t* fb = oc_fb_get_info();
    if (!fb || !fb->addr) return -1;
    if (!oc_g_active_renderer) return -2;
    u32 cw  = oc_g_active_renderer->cell_w;
    u32 chh = oc_g_active_renderer->cell_h;
    g_console.cols = fb->width  / cw;
    g_console.rows = fb->height / chh;
    g_console.cur_x = 0;
    g_console.cur_y = 0;
    g_console.fg_pixel = oc_fb_rgb(0xE0, 0xE0, 0xE0);
    g_console.bg_pixel = oc_fb_rgb(0x10, 0x10, 0x14);
    g_console.cursor_visible = 1;
    oc_console_clear();
    return 0;
}

void oc_console_reset(u32 fg_pixel, u32 bg_pixel) {
    g_console.fg_pixel = fg_pixel;
    g_console.bg_pixel = bg_pixel;
    g_console.cur_x = 0;
    g_console.cur_y = 0;
    oc_console_clear();
}

void oc_console_show_cursor(int visible) {
    g_console.cursor_visible = visible ? 1 : 0;
}

void oc_console_move_cursor(u32 x, u32 y) {
    if (x >= g_console.cols) x = g_console.cols - 1;
    if (y >= g_console.rows) y = g_console.rows - 1;
    g_console.cur_x = x;
    g_console.cur_y = y;
}

void oc_console_clear(void) {
    oc_fb_clear(g_console.bg_pixel);
    g_console.cur_x = 0;
    g_console.cur_y = 0;
}

void oc_console_putc_at(u32 x, u32 y, char ch, u32 fg_pixel, u32 bg_pixel) {
    if (x >= g_console.cols || y >= g_console.rows) return;
    draw_cell(x, y, ch, fg_pixel, bg_pixel);
}

void oc_console_putc(char ch) {
    /* First, forward to L1 hook (best-effort). */
    oc_ext_console_hook((u8)ch);

    /* Helper: erase the cursor at the current cell. */
    #define ERASE_CURSOR() do {                                  \
        if (g_console.cursor_visible)                            \
            draw_cell(g_console.cur_x, g_console.cur_y, ' ',     \
                      g_console.fg_pixel, g_console.bg_pixel);   \
    } while (0)

    switch (ch) {
    case '\n':
        ERASE_CURSOR();
        g_console.cur_x = 0;
        ++g_console.cur_y;
        break;
    case '\r':
        ERASE_CURSOR();
        g_console.cur_x = 0;
        break;
    case '\t':
        ERASE_CURSOR();
        /* Tab to next multiple of 4 columns. */
        g_console.cur_x = (g_console.cur_x + 4) & ~(u32)3;
        if (g_console.cur_x >= g_console.cols) {
            g_console.cur_x = 0;
            ++g_console.cur_y;
        }
        break;
    case '\b':
        ERASE_CURSOR();
        if (g_console.cur_x > 0) --g_console.cur_x;
        /* Leave the new cell alone - the cursor draw below will overwrite it. */
        break;
    default:
        draw_cell(g_console.cur_x, g_console.cur_y, ch,
                  g_console.fg_pixel, g_console.bg_pixel);
        ++g_console.cur_x;
        if (g_console.cur_x >= g_console.cols) {
            g_console.cur_x = 0;
            ++g_console.cur_y;
        }
        break;
    }

    /* Scroll if we ran off the bottom. */
    if (g_console.cur_y >= g_console.rows) {
        scroll_up_one();
        g_console.cur_y = g_console.rows - 1;
    }

    /* Draw the cursor (underscore) at the current cell if visible. */
    if (g_console.cursor_visible) {
        draw_cell(g_console.cur_x, g_console.cur_y, '_',
                  g_console.fg_pixel, g_console.bg_pixel);
    }
    #undef ERASE_CURSOR
}

void oc_console_puts(const char* s) {
    while (*s) oc_console_putc(*s++);
}

oc_console_t* oc_console_get(void) { return &g_console; }

u32 oc_color_rgb(u8 r, u8 g, u8 b) { return oc_fb_rgb(r, g, b); }
