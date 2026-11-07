/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03
 * File: services/display/disp_ext.h
 * Purpose: display-service extension accessors - framebuffer info and
 * active-renderer swap.
 *
 * Formerly sections 1-2 of the l1_ext.h grab-bag (services/l1/), split
 * into the owning display root service in the WP-03 L0 restructure.
 * The console-output hook that also lived behind l1_ext.h moved DOWN
 * into the display driver in G1 already (screen_console_set_hook /
 * screen_console_call_hook in drivers/display/).
 */
#ifndef OC_DISP_EXT_H
#define OC_DISP_EXT_H

#include "types.h"
#include "screen_fb.h"
#include "screen_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the framebuffer descriptor (addr, pitch, width, height, bpp,
 * masks); L1 writes pixels directly to addr. */
const screen_fb_info_t* l1_ext_fb_get_info(void);

/* Swaps in a caller-supplied renderer (own draw_glyph + cell dimensions)
 * and returns the previous renderer so the caller can restore it. */
screen_renderer_t* l1_ext_set_renderer(screen_renderer_t* new_renderer);

#ifdef __cplusplus
}
#endif

#endif /* OC_DISP_EXT_H */
