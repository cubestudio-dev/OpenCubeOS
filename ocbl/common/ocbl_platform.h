/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_platform.h
 * Purpose: the flavor backend contract (B5-2).  Everything the boot
 *          flow needs from the FIRMWARE lives behind these functions;
 *          common code calls only these, the two flavors implement
 *          them with whatever their firmware offers:
 *
 *            BIOS flavor (ocbl/bios/ocbl_con_bios.c):
 *              console - VGA text 0xB8000 (int 10h mode set)
 *              clock   - int 1Ah ticks (54.9 ms) scaled to ms
 *              keys    - int 16h PS/2 + int 14h serial, both sources
 *            UEFI flavor (ocbl/efi/ocbl_con_efi.c):
 *              console - ConOut SetCursorPosition/SetAttribute
 *              clock   - boot-services timer event (100 ms period)
 *              keys    - ReadKeyStroke + the 16550 serial line
 *
 *          The serial RENDER (menu rows on the serial transcript)
 *          stays in common (ocbl_menu.c serial_at) - the serial line
 *          is plain 16550 port I/O under BOTH firmwares.
 */
#ifndef OCBL_PLATFORM_H
#define OCBL_PLATFORM_H

#include "ocbl_types.h"

/* the three-key contract (scan codes on BIOS, Unicode/scan on UEFI -
 * both backends collapse to these) */
#define OCBL_KEY_UP    0x48
#define OCBL_KEY_DOWN  0x50
#define OCBL_KEY_ENTER 0x1C

/* Prepare the console for the menu (text mode, clear, cursor off). */
void ocbl_plat_con_init(void);

/* Draw `s` at (row, col).  Rows 0..24, cols 0..79 (the menu layout
 * is 80x25 on both flavors).  Style keeps the B3 look byte-stable:
 * ENTRY rows get the full-width padded bar (inverse when selected). */
#define OCBL_CON_ENTRY   0   /* padded line, normal attr    */
#define OCBL_CON_SELECT  1   /* padded line, inverse attr   */
#define OCBL_CON_BRIGHT  2   /* no pad, bright attr (title) */
#define OCBL_CON_PLAIN   3   /* no pad, normal attr (hints) */
void ocbl_plat_con_line(u8 row, u8 col, const char* s, u8 style);

/* Milliseconds since an arbitrary epoch (wraps at 2^32 - fine for
 * the 10 s countdown; the BIOS source is 54.9 ms grain). */
u32 ocbl_plat_ms(void);

/* Poll the keyboard: OCBL_KEY_UP / OCBL_KEY_DOWN / OCBL_KEY_ENTER,
 * or -1 when nothing (or an unregistered key) is pending. */
int ocbl_plat_getkey(void);

#endif /* OCBL_PLATFORM_H */
