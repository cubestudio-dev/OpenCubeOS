/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - VGA colour text mode driver (WP-01-tree).
 *
 * 80x25 text cells at physical 0xB8000, each cell a 16-bit word
 * (ASCII byte in the low byte, attribute in the high byte). The boot
 * trampoline identity-maps the first 1 GiB, so the linear address
 * equals the physical one. 0x0F = light grey on black.
 *
 * drv_video_init() blanks the whole screen and parks the print
 * position at the top-left, so whatever the firmware or the
 * bootloader left behind (SeaBIOS boot lines, GRUB's
 * `Booting '<title>'`) is gone before the kernel prints anything:
 * the first visible line on a real machine is the kernel's own first
 * line.
 *
 * Naming: [class]_[category]_[role] - drv (driver), video (device
 * class), vga (the specific standard this driver targets).
 */

#ifndef DRV_VIDEO_VGA_H
#define DRV_VIDEO_VGA_H

/* Clear the 80x25 text screen and reset the print position to the
 * top-left cell. */
void drv_video_init(void);

/* Print one character at the cursor position. '\n' moves to the next
 * line; printable bytes are written with the default attribute. */
void drv_video_putc(char c);

/* Print a NUL-terminated string, byte by byte. */
void drv_video_puts(const char *s);

/* Park the blinking hardware cursor right after the last printed
 * character, so no stray cursor is left mid-screen. */
void drv_video_sync_cursor(void);

#endif /* DRV_VIDEO_VGA_H */
