/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL VGA colour text mode driver (WP-02-ocbl).
 *
 * 80x25 text cells at physical 0xB8000, each cell a 16-bit word
 * (ASCII byte in the low byte, attribute in the high byte). Used by
 * the BIOS path only (stage 64-bit C code) for on-screen progress
 * before the kernel takes over; the BIOS path runs in VGA text mode
 * by construction, so these writes are visible.
 *
 * Compiled by the BIOS toolchain only (gcc ELF64). The UEFI path has
 * its own driver (drv_video_uefi_gop.c); the two never include each
 * other.
 *
 * Naming: [class]_[category]_[role] - drv (driver), video (device
 * class), vga (the specific standard this driver targets).
 */

#ifndef DRV_VIDEO_VGA_H
#define DRV_VIDEO_VGA_H

#include <stdint.h>

/* Clear the 80x25 text screen and reset the print position to the
 * top-left cell. */
void drv_video_init(void);

/* Print one character at the cursor position. '\n' moves to the next
 * line. */
void drv_video_putc(char c);

/* Print a NUL-terminated string, byte by byte. */
void drv_video_puts(const char *s);

#endif /* DRV_VIDEO_VGA_H */
