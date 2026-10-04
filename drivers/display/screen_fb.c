/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/fb.c
 */
#include "screen_fb.h"
#include "arch_multiboot2.h"
#include "lib_string.h"

static screen_fb_info_t g_fb;

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
    u8* row = g_fb.addr + (u64)y * g_fb.pitch;
    u32* px = (u32*)(row + (u64)x * 4);
    *px = pixel;
}

void screen_fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 pixel) {
    /* Clip to framebuffer. */
    if (x >= g_fb.width || y >= g_fb.height) return;
    if (x + w > g_fb.width)  w = g_fb.width  - x;
    if (y + h > g_fb.height) h = g_fb.height - y;

    for (u32 j = 0; j < h; ++j) {
        u8*  row = g_fb.addr + (u64)(y + j) * g_fb.pitch;
        u32* px  = (u32*)(row + (u64)x * 4);
        for (u32 i = 0; i < w; ++i) px[i] = pixel;
    }
}

void screen_fb_blit(u32 x, u32 y, u32 w, u32 h, const u32* src, u32 src_pitch_bytes) {
    if (x >= g_fb.width || y >= g_fb.height) return;
    if (x + w > g_fb.width)  w = g_fb.width  - x;
    if (y + h > g_fb.height) h = g_fb.height - y;

    for (u32 j = 0; j < h; ++j) {
        u8*        dst_row = g_fb.addr + (u64)(y + j) * g_fb.pitch;
        const u32* src_row = (const u32*)((const u8*)src + (u64)j * src_pitch_bytes);
        u32*       dpx     = (u32*)(dst_row + (u64)x * 4);
        for (u32 i = 0; i < w; ++i) dpx[i] = src_row[i];
    }
}

void screen_fb_clear(u32 pixel) {
    screen_fb_fill_rect(0, 0, g_fb.width, g_fb.height, pixel);
}
