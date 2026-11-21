/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_fb.h
 * Purpose: the firmware-agnostic framebuffer descriptor handed to the
 *          multiboot2 tag-8 builder (ocbl_load.c).  The BIOS flavor
 *          fills it from VBE mode info, the UEFI flavor from GOP
 *          (B5-5) - one shape, two backends, same mb2 tag.
 */
#ifndef OCBL_FB_H
#define OCBL_FB_H

#include "ocbl_types.h"

typedef struct ocbl_fb_info {
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u8  bpp;
    u8  red_pos, red_size;
    u8  green_pos, green_size;
    u8  blue_pos, blue_size;
} ocbl_fb_info_t;

/* Since WP-runtime-hardening the UEFI track fills this from the
 * OCRMECL handoff (ocbl_raw.c) - the GOP code lives in OCRMECL now. */

#endif /* OCBL_FB_H */
