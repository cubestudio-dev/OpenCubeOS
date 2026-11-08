/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - VGA colour text mode driver (WP-01-tree).
 *
 * Implementation for the interface declared in drv_video_vga.h. The
 * CRTC index/data ports are used only to place the hardware cursor;
 * the screen cells are written directly through the memory-mapped
 * text buffer at 0xB8000.
 */

#include <stdint.h>
#include <stddef.h>

#include "kernel/core_io_port.h"
#include "drivers/drv_video_vga.h"

#define DRV_VIDEO_MEM_BASE 0xB8000
#define DRV_VIDEO_COLS     80U
#define DRV_VIDEO_ROWS     25U
#define DRV_VIDEO_ATTR     0x0F00U /* light grey on black */

/* CRTC index/data ports, used only to place the hardware cursor. */
#define DRV_VIDEO_CRTC_INDEX 0x3D4U
#define DRV_VIDEO_CRTC_DATA  0x3D5U

static uint16_t *const drv_video_mem = (uint16_t *)DRV_VIDEO_MEM_BASE;
static size_t drv_video_row;
static size_t drv_video_col;

/* Blank every cell and park the print position at the top-left, so
 * output starts at screen line 1 no matter what the firmware or the
 * bootloader left behind. */
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
    /* the four-line banner never reaches the bottom row; staying put
     * there is the defined fallback */
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

void drv_video_sync_cursor(void)
{
    uint16_t pos = (uint16_t)(drv_video_row * DRV_VIDEO_COLS + drv_video_col);

    core_io_out8(DRV_VIDEO_CRTC_INDEX, 0x0FU); /* cursor loc low */
    core_io_out8(DRV_VIDEO_CRTC_DATA, (uint8_t)(pos & 0x00FFU));
    core_io_out8(DRV_VIDEO_CRTC_INDEX, 0x0EU); /* cursor loc high */
    core_io_out8(DRV_VIDEO_CRTC_DATA, (uint8_t)((pos >> 8) & 0x00FFU));
}
