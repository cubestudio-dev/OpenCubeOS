/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_vga.c
 * Purpose: 80x25 text-mode renderer (see ocbl_vga.h).
 */
#include "ocbl_vga.h"
#include "ocbl_bios.h"

#ifdef OCBL_FLAVOR_BIOS

#define VMEM ((volatile u16*)OCBL_VGA_MEM)

void ocbl_vga_clear(void)
{
    for (u32 i = 0; i < (u32)OCBL_VGA_COLS * OCBL_VGA_ROWS; i++)
        VMEM[i] = (u16)(OCBL_VGA_ATTR_NORMAL << 8 | ' ');
}

void ocbl_vga_puts(u8 row, u8 col, u8 attr, const char* s)
{
    if (row >= OCBL_VGA_ROWS)
        return;
    u32 idx = (u32)row * OCBL_VGA_COLS + col;
    while (*s && col < OCBL_VGA_COLS) {
        VMEM[idx++] = (u16)(attr << 8 | (u8)*s++);
        col++;
    }
}

void ocbl_vga_padline(u8 row, u8 attr)
{
    if (row >= OCBL_VGA_ROWS)
        return;
    u32 idx = (u32)row * OCBL_VGA_COLS;
    for (u32 c = 0; c < OCBL_VGA_COLS; c++)
        VMEM[idx + c] = (u16)(attr << 8 | ' ');
}

void ocbl_vga_cursor_off(void)
{
    /* int 10h AH=01h: CH bit5 set hides the cursor */
    ocbl_bios_regs_t r = {0};
    r.eax = 0x0100;
    r.ecx = 0x2000;
    ocbl_bios_int(0x10, &r);
}

void ocbl_vga_text_mode(void)
{
    /* int 10h AH=00h AL=03h: 80x25 16-color text - SeaBIOS may boot
     * in a graphics mode (logo), and the 0xB8000 renderer needs the
     * classic text plane (live-caught: screendump showed graphics
     * noise, not the menu) */
    ocbl_bios_regs_t r = {0};
    r.eax = 0x0003;
    ocbl_bios_int(0x10, &r);
}


#endif /* OCBL_FLAVOR_BIOS */
