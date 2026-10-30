/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_vga.h
 * Purpose: 80x25 VGA text output for the menu (BIOS flavor; the UEFI
 *          flavor renders serial-only until the GOP backend lands in
 *          the UEFI batch).  Direct 0xB8000 writes - the identity page
 *          tables map the first 4 GiB present|writable, so no BIOS
 *          call is involved in rendering.
 */
#ifndef OCBL_VGA_H
#define OCBL_VGA_H

#include "ocbl_types.h"

#ifdef OCBL_FLAVOR_BIOS

#define OCBL_VGA_MEM   0xB8000u
#define OCBL_VGA_COLS  80
#define OCBL_VGA_ROWS  25

/* attribute nibbles: fg | bg<<4 */
#define OCBL_VGA_ATTR_NORMAL  0x07   /* light gray on black */
#define OCBL_VGA_ATTR_BRIGHT  0x0F   /* white on black      */
#define OCBL_VGA_ATTR_SELECT  0x70   /* black on light gray */

void ocbl_vga_clear(void);
void ocbl_vga_puts(u8 row, u8 col, u8 attr, const char* s);
void ocbl_vga_padline(u8 row, u8 attr);       /* blank a full row */
void ocbl_vga_cursor_off(void);
void ocbl_vga_text_mode(void);                /* int10h mode 3 */

#endif /* OCBL_FLAVOR_BIOS */
#endif /* OCBL_VGA_H */
