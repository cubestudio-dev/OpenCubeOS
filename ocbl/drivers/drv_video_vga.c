/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL VGA colour text mode driver (WP-02-ocbl).
 *
 * Implementation for the interface declared in drv_video_vga.h.
 * BIOS path only: the boot trampoline identity-maps the first 1 GiB,
 * so the linear address equals the physical one. 0x0A = bright green
 * on black for OCBL's own progress lines; the kernel re-inits the
 * screen with its own attribute when it starts.
 */

#include <stdint.h>
#include <stddef.h>

#include "lib/core_io_port.h"
#include "drivers/drv_video_vga.h"

#define DRV_VIDEO_MEM_BASE 0xB8000
#define DRV_VIDEO_COLS     80U
#define DRV_VIDEO_ROWS     25U
#define DRV_VIDEO_ATTR     0x0A00U /* bright green on black */

static uint16_t *const drv_video_mem = (uint16_t *)DRV_VIDEO_MEM_BASE;
static size_t drv_video_row;
static size_t drv_video_col;

/* Blank every cell and park the print position at the top-left. */
void drv_video_init(void)
{
    for (size_t i = 0U; i < DRV_VIDEO_COLS * DRV_VIDEO_ROWS; ++i) {
        drv_video_mem[i] = (uint16_t)' ' | DRV_VIDEO_ATTR;
    }
    drv_video_row = 0U;
    drv_video_col = 0U;
}

static void drv_video_newline(void)
{
    drv_video_col = 0U;
    if (drv_video_row + 1U < DRV_VIDEO_ROWS) {
        ++drv_video_row;
    }
    /* OCBL prints a handful of progress lines; staying put on the
     * bottom row is the defined fallback */
}

void drv_video_putc(char c)
{
    if (c == '\n') {
        drv_video_newline();
        return;
    }
    if (drv_video_col >= DRV_VIDEO_COLS) {
        drv_video_newline();
    }
    drv_video_mem[drv_video_row * DRV_VIDEO_COLS + drv_video_col] =
        (uint16_t)(unsigned char)c | DRV_VIDEO_ATTR;
    ++drv_video_col;
}

void drv_video_puts(const char *s)
{
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        drv_video_putc(s[i]);
    }
}
