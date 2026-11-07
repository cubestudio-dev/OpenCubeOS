/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/bios/ocbl_con_bios.c
 * Purpose: the BIOS flavor of the platform console/clock/keyboard
 *          backend (ocbl_platform.h).  Extracted verbatim from
 *          ocbl_menu.c in B5-2 - the VGA renderer, the int 1Ah tick
 *          clock and the int 16h + int 14h key sources - so the menu
 *          state machine became firmware-agnostic.
 */
#include "ocbl_platform.h"
#include "ocbl_vga.h"
#include "ocbl_bios.h"

/* ---- clock: int 1Ah AH=00h ticks since midnight (54.9 ms) ---- */
u32 ocbl_plat_ms(void)
{
    ocbl_bios_regs_t r = {0};
    r.eax = 0x0000;
    ocbl_bios_int(0x1A, &r);
    u32 ticks = ((u32)r.ecx << 16) | (r.edx & 0xFFFF);
    /* 18.2065 Hz -> ms; u32 wrap at ~49 days is fine for the menu */
    return ticks * 55u;
}

/* ---- keyboard: int 16h (PS/2) + int 14h (serial line) ----
 * Both sources collapse to the same three-key contract. */
int ocbl_plat_getkey(void)
{
    ocbl_bios_regs_t r = {0};
    r.eax = 0x0100;                     /* AH=01: peek, ZF=0 if ready */
    ocbl_bios_int(0x16, &r);
    if (!(r.flags & 0x40)) {
        r.eax = 0x0000;                 /* AH=00: consume */
        ocbl_bios_int(0x16, &r);
        u16 k = (u16)(r.eax & 0xFFFF);
        u8 sc = (u8)(k >> 8);
        if (sc == OCBL_KEY_UP || sc == OCBL_KEY_DOWN ||
            sc == OCBL_KEY_ENTER)
            return sc;
        return -1;                      /* registered: ignore */
    }
    /* serial console keying (16550 line status bit0 = data ready) */
    ocbl_bios_regs_t s = {0};
    s.eax = 0x0300;                     /* AH=03: line status */
    s.edx = 0;
    ocbl_bios_int(0x14, &s);
    if (s.eax & 0x0100) {               /* AH bit0: receive ready */
        s.eax = 0x0200;                 /* AH=02: read char */
        s.edx = 0;
        ocbl_bios_int(0x14, &s);
        char c = (char)(s.eax & 0xFF);
        if (c == 'A')
            return OCBL_KEY_UP;         /* CSI tail after ESC+'[' */
        if (c == 'B')
            return OCBL_KEY_DOWN;
        if (c == '\r' || c == '\n')
            return OCBL_KEY_ENTER;
        return -1;
    }
    return -1;
}

/* ---- rendering ---- */
void ocbl_plat_con_init(void)
{
    ocbl_vga_text_mode();
    ocbl_vga_clear();
    ocbl_vga_cursor_off();
}

void ocbl_plat_con_line(u8 row, u8 col, const char* s, u8 style)
{
    u8 attr = OCBL_VGA_ATTR_NORMAL;
    if (style == OCBL_CON_SELECT)
        attr = OCBL_VGA_ATTR_SELECT;
    else if (style == OCBL_CON_BRIGHT)
        attr = OCBL_VGA_ATTR_BRIGHT;
    if (style == OCBL_CON_ENTRY || style == OCBL_CON_SELECT)
        ocbl_vga_padline(row, attr);
    ocbl_vga_puts(row, col, attr, s);
}
