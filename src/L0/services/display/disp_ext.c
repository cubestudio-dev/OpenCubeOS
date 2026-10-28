/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03
 * File: services/display/disp_ext.c
 * Purpose: display-service extension accessors (moved verbatim from
 * services/l1/l1_ext.c in the WP-03 L0 restructure).
 *   - fb accessor returns &g_fb via screen_fb_get_info()
 *   - renderer swap delegates to screen_renderer_set_active()
 */
#include "disp_ext.h"
#include "screen_console.h"

/* ---- 1. Framebuffer access ---- */
const screen_fb_info_t* l1_ext_fb_get_info(void) {
    return screen_fb_get_info();
}

/* ---- 2. Renderer replacement ---- */
screen_renderer_t* l1_ext_set_renderer(screen_renderer_t* new_renderer) {
    return screen_renderer_set_active(new_renderer);
}
