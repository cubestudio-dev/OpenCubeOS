/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/fb.h
 * Purpose: Framebuffer primitives: put_pixel, fill_rect, blit.
 *
 * The framebuffer is the single L0 graphics surface. L1 extensions can
 * fetch a pointer to it via screen_fb_get_info() (see ext.h).
 *
 * Color packing: callers pass RGB triplets; fb packs them into the
 * framebuffer's native pixel layout using the bit masks that GRUB
 * reported (multiboot2 framebuffer tag). This means we correctly handle
 * both BGRX (BIOS VBE) and RGBX (UEFI GOP) layouts.
 */
#ifndef OC_FB_H
#define OC_FB_H

#include "types.h"

typedef struct screen_fb_info {
    u8*    addr;           /* virtual framebuffer base */
    u32    pitch;         /* bytes per scanline */
    u32    width;         /* pixels per scanline */
    u32    height;        /* scanlines */
    u8     bpp;           /* bits per pixel (we support 32) */
    u8     red_pos,   red_size;
    u8     green_pos, green_size;
    u8     blue_pos,  blue_size;
} screen_fb_info_t;

/* Initialize from a parsed multiboot2 framebuffer tag. Returns 0 on success. */
int  screen_fb_init(const void* arch_multiboot2_fb_tag);

/* Query the framebuffer info - used by L1 extensions. */
const screen_fb_info_t* screen_fb_get_info(void);

/* Pack an RGB triplet into a framebuffer-native pixel value. */
u32  screen_fb_rgb(u8 r, u8 g, u8 b);

/* Primitives. */
void screen_fb_put_pixel (u32 x, u32 y, u32 pixel);
void screen_fb_fill_rect  (u32 x, u32 y, u32 w, u32 h, u32 pixel);
void screen_fb_blit       (u32 x, u32 y, u32 w, u32 h, const u32* src, u32 src_pitch_bytes);

/* Convenience: clear entire framebuffer to a single pixel value. */
void screen_fb_clear(u32 pixel);

#endif /* OC_FB_H */
