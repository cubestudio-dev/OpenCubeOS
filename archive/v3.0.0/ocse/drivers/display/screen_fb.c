/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: drivers/display/screen_fb.c
 */
#include "screen_fb.h"
#include "arch_multiboot2.h"
#include "lib_string.h"
#include "mem_heap.h"     /* WP06f: the double-buffer allocation */

static screen_fb_info_t g_fb;

/* ---- 3.0.0-WP06f: the RAM back buffer (double buffering) --------------
 * While attached, every draw primitive renders into g_shadow (a
 * width*4-pitch mirror of the screen) instead of the live framebuffer;
 * screen_fb_present() publishes only the rows that differ.  A frame
 * reaches the visible surface as one final write per pixel - the
 * blank-then-repaint intermediate states that flickered are gone. */
static u8* g_shadow;          /* kmalloc'd W*H*4 back buffer (or NULL) */
static int g_shadow_on;       /* 1 = draws route to the back buffer   */

static u8* draw_base(void)
{
    return g_shadow_on && g_shadow ? g_shadow : g_fb.addr;
}

static u32 draw_pitch(void)
{
    /* the back buffer is always tightly packed */
    return (g_shadow_on && g_shadow) ? g_fb.width * 4u : g_fb.pitch;
}

int screen_fb_init(const void* arch_multiboot2_fb_tag) {
    const arch_multiboot2_fb_tag_t* f = (const arch_multiboot2_fb_tag_t*)arch_multiboot2_fb_tag;
    if (!f) return -1;
    if (f->framebuffer_type != 1) {
        /* Only RGB (type 1) supported in WP-01. Text (2) and indexed (0) refused. */
        return -2;
    }
    if (f->framebuffer_bpp != 32) {
        return -3;
    }
    /* BUG-0322 (A7-13): validate the geometry before publishing it.
     * addr==0 previously sailed through here (only console_init checked
     * it later), and a pitch smaller than width*4 made every row walk
     * into the previous/next row (or off the mapped extent). */
    if (f->framebuffer_addr == 0) return -4;
    if (f->framebuffer_width == 0 || f->framebuffer_height == 0) return -5;
    if (f->framebuffer_pitch < (u32)f->framebuffer_width * 4u) return -6;
    g_fb.addr   = (u8*)(uintptr_t)f->framebuffer_addr;
    g_fb.pitch  = f->framebuffer_pitch;
    g_fb.width  = f->framebuffer_width;
    g_fb.height = f->framebuffer_height;
    g_fb.bpp    = f->framebuffer_bpp;
    g_fb.red_pos    = f->red_field_position;
    g_fb.red_size   = f->red_mask_size;
    g_fb.green_pos  = f->green_field_position;
    g_fb.green_size = f->green_mask_size;
    g_fb.blue_pos   = f->blue_field_position;
    g_fb.blue_size  = f->blue_mask_size;
    /* WP-04 fix: GRUB sometimes doesn't fill mask sizes. Default to 8 for 32bpp. */
    if (g_fb.red_size == 0 && g_fb.bpp == 32) g_fb.red_size = 8;
    if (g_fb.green_size == 0 && g_fb.bpp == 32) g_fb.green_size = 8;
    if (g_fb.blue_size == 0 && g_fb.bpp == 32) g_fb.blue_size = 8;
    return 0;
}

const screen_fb_info_t* screen_fb_get_info(void) {
    return &g_fb;
}

u32 screen_fb_rgb(u8 r, u8 g, u8 b) {
    /* Scale each channel up to its mask width. We assume 8-bit per channel
     * input and scale down to the framebuffer's mask size (typically 8). */
    u32 rv = (g_fb.red_size   >= 8) ? (u32)r : ((u32)r >> (8 - g_fb.red_size));
    u32 gv = (g_fb.green_size >= 8) ? (u32)g : ((u32)g >> (8 - g_fb.green_size));
    u32 bv = (g_fb.blue_size  >= 8) ? (u32)b : ((u32)b >> (8 - g_fb.blue_size));
    return (rv << g_fb.red_pos) | (gv << g_fb.green_pos) | (bv << g_fb.blue_pos);
}

void screen_fb_put_pixel(u32 x, u32 y, u32 pixel) {
    if (x >= g_fb.width || y >= g_fb.height) return;
    u8* row = draw_base() + (u64)y * draw_pitch();
    u32* px = (u32*)(row + (u64)x * 4);
    *px = pixel;
}

void screen_fb_write_px(u32 x, u32 y, u32 pixel) {
    /* WP06f: unconditional write for glyph blitters / the cursor sprite -
     * identical routing to put_pixel (kept separate so the call sites
     * document that fg AND bg pixels land, never a layered overlay). */
    if (x >= g_fb.width || y >= g_fb.height) return;
    u8* row = draw_base() + (u64)y * draw_pitch();
    u32* px = (u32*)(row + (u64)x * 4);
    *px = pixel;
}

void screen_fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 pixel) {
    /* Clip to framebuffer.
     * BUG-0120 FIX: the old clipping compared x + w > width in u32, so a
     * huge w (x + w wrapping past 2^32) bypassed the clip and the fill
     * loop wrote far outside the framebuffer. x < width is already
     * established here, so compare against width - x (no overflow). */
    if (x >= g_fb.width || y >= g_fb.height) return;
    if (w > g_fb.width  - x) w = g_fb.width  - x;
    if (h > g_fb.height - y) h = g_fb.height - y;

    u8* base = draw_base();
    u32 pitch = draw_pitch();
    for (u32 j = 0; j < h; ++j) {
        u8*  row = base + (u64)(y + j) * pitch;
        u32* px  = (u32*)(row + (u64)x * 4);
        for (u32 i = 0; i < w; ++i) px[i] = pixel;
    }
}

void screen_fb_blit(u32 x, u32 y, u32 w, u32 h, const u32* src, u32 src_pitch_bytes) {
    /* BUG-0120 FIX: same u32-wraparound clip bypass as fill_rect. */
    if (x >= g_fb.width || y >= g_fb.height) return;
    if (w > g_fb.width  - x) w = g_fb.width  - x;
    if (h > g_fb.height - y) h = g_fb.height - y;

    u8* base = draw_base();
    u32 pitch = draw_pitch();
    for (u32 j = 0; j < h; ++j) {
        u8*        dst_row = base + (u64)(y + j) * pitch;
        const u32* src_row = (const u32*)((const u8*)src + (u64)j * src_pitch_bytes);
        u32*       dpx     = (u32*)(dst_row + (u64)x * 4);
        for (u32 i = 0; i < w; ++i) dpx[i] = src_row[i];
    }
}

void screen_fb_clear(u32 pixel) {
    screen_fb_fill_rect(0, 0, g_fb.width, g_fb.height, pixel);
}

int screen_fb_shadow_attach(void) {
    if (g_shadow) { g_shadow_on = 1; return 0; }
    if (g_fb.width == 0 || g_fb.height == 0) return -1;
    g_shadow = kmalloc((u64)g_fb.width * g_fb.height * 4u);
    if (!g_shadow) return -1;
    /* start as a faithful copy of what is on screen so a partial
     * repaint + present never flashes stale content */
    for (u32 y = 0; y < g_fb.height; ++y)
        memcpy(g_shadow + (u64)y * g_fb.width * 4u,
               g_fb.addr + (u64)y * g_fb.pitch,
               (usize)g_fb.width * 4u);
    g_shadow_on = 1;
    return 0;
}

void screen_fb_shadow_detach(void) {
    g_shadow_on = 0;
}

int screen_fb_shadow_active(void) {
    return g_shadow_on && g_shadow ? 1 : 0;
}

void screen_fb_present(void) {
    /* Publish the finished frame: copy ONLY the rows that differ
     * (the local refresh - a pointer move rewrites ~20 of 600 rows,
     * a progress tick ~2).  With no back buffer this is a no-op: the
     * primitives already wrote the live surface directly. */
    if (!g_shadow_on || !g_shadow) return;
    u32 pitch = g_fb.width * 4u;
    for (u32 y = 0; y < g_fb.height; ++y) {
        u8* srow = g_shadow + (u64)y * pitch;
        u8* frow = g_fb.addr + (u64)y * g_fb.pitch;
        if (memcmp(srow, frow, pitch) != 0)
            memcpy(frow, srow, pitch);
    }
}

/* ---- 3.0.0-WP06f: the standard arrow pointer (13x16) -------------------
 * The classic arrow silhouette: a solid black triangular head with its
 * left edge vertical, the hypotenuse down to the tip at row 10, and a
 * 3px stem descending to row 15.  One-pixel white outline all around
 * (so the pointer stays visible on dark backgrounds); everything else
 * transparent - the underlying frame shows through untouched.
 *
 *   bit=1 in k_cursor_mask : this pixel belongs to the pointer
 *   bit=1 in k_cursor_white: outline pixel (white); 0 = fill (black)
 *
 * (The old pointer was three stacked fill_rects - a chunky block
 * "arrow" with no outline that read as a random black blob.) */
#define CURSOR_W 13u
#define CURSOR_H 16u
static const u16 k_cursor_mask[CURSOR_H] = {
    0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80, 0xFFC0,
    0xFFE0, 0xFFF0, 0xFFF8, 0xFFF0, 0xF800, 0xF800, 0xF800, 0xF800,
};
static const u16 k_cursor_white[CURSOR_H] = {
    0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080, 0x8040,
    0x8020, 0x8010, 0x8008, 0x87F0, 0x8800, 0x8800, 0x8800, 0x8800,
};

void screen_fb_draw_cursor(u32 x, u32 y) {
    u32 white = screen_fb_rgb(0xFF, 0xFF, 0xFF);
    u32 black = screen_fb_rgb(0x10, 0x10, 0x10);
    for (u32 r = 0; r < CURSOR_H; r++) {
        u32 py = y + r;
        if (py >= g_fb.height) break;
        u16 m = k_cursor_mask[r];
        u16 w = k_cursor_white[r];
        for (u32 c = 0; c < CURSOR_W; c++) {
            if (!(m & (0x8000u >> c))) continue;   /* transparent */
            u32 px = x + c;
            if (px >= g_fb.width) break;
            screen_fb_write_px(px, py, (w & (0x8000u >> c)) ? white : black);
        }
    }
}
