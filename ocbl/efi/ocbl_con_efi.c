/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/ocbl_con_efi.c
 * Purpose: the UEFI flavor of the platform console/clock/keyboard
 *          backend (ocbl_platform.h).  Console = the firmware ConOut
 *          (80x25 text, SetAttribute for the highlight bar - OVMF
 *          mirrors it to the serial line, so the acceptance
 *          transcripts show the same menu as the BIOS flavor's);
 *          clock = a boot-services timer event polled at 100 ms;
 *          keys = ReadKeyStroke (the PS/2 path under OVMF) plus the
 *          16550 serial line read directly - the same dual-source,
 *          three-key contract the BIOS flavor serves.
 */
#include "efi.h"
#include "../common/ocbl_platform.h"
#include "../common/ocbl_serial.h"

/* ---- clock: 100 ms timer event ---- */
static efi_event g_timer;
static u32 g_ticks;                  /* 100 ms units */

static void timer_ensure(void)
{
    if (g_timer)
        return;
    if (ocbl_efi_bs->create_event(0x80000000 /* EVT_TIMER */, 0,
                                  0, 0, &g_timer) == EFI_SUCCESS)
        ocbl_efi_bs->set_timer(g_timer, 1 /* Periodic */, 100 * 1000);
}

u32 ocbl_plat_ms(void)
{
    timer_ensure();
    if (g_timer) {
        while (ocbl_efi_bs->check_event(g_timer) == EFI_SUCCESS)
            g_ticks++;
    }
    return g_ticks * 100u;
}

/* ---- keyboard: the 16550 line FIRST, then ReadKeyStroke ----
 * B5-5 live-caught: OVMF's Terminal/ConIn driver reads the SAME RBR
 * when ReadKeyStroke is called - calling it first steals a byte of
 * the serial CSI sequence (ESC '[' tail), and neither consumer ever
 * sees a complete key (the injected arrow silently vanished, the pick
 * fell back to the default entry).  Order matters: drain OUR OWN
 * polled serial byte first (it needs no firmware driver), and only
 * then ask ConIn for PS/2 events - those live in the firmware's own
 * queue and cannot be stolen by us. */
/* B5-5 bring-up: key-path counters - one live run tells which of
 * the two sources is silent (diagnostics only, zero behavior).
 * ser_bytes  - raw bytes our own 16550 poll consumed
 * conin_calls- ReadKeyStroke invocations
 * conin_hits- ReadKeyStroke returns with EFI_SUCCESS */
int ocbl_plat_getkey(void)
{
    /* serial console keying - the 16550 is plain port I/O under UEFI
     * exactly as under BIOS; the harness writes the same CSI tails */
    u8 lsr;
    __asm__ volatile("inb %1, %0" : "=a"(lsr) : "d"((u16)0x3FD));
    if (lsr & 0x01) {
        char c;
        __asm__ volatile("inb %1, %0" : "=a"(c) : "d"((u16)0x3F8));
        if (c == 'A')
            return OCBL_KEY_UP;      /* CSI tail after ESC+'[' */
        if (c == 'B')
            return OCBL_KEY_DOWN;
        if (c == '\r' || c == '\n')
            return OCBL_KEY_ENTER;
        /* ESC and '[' are consumed and ignored - the tail decides */
    }
    /* firmware keyboard (PS/2 through OVMF) */
    efi_input_key k;
    if (ocbl_efi_st->con_in &&
        ocbl_efi_st->con_in->read_keystroke(ocbl_efi_st->con_in, &k)
            == EFI_SUCCESS) {
        if (k.scan_code == 1)
            return OCBL_KEY_UP;      /* SCAN_UP   */
        if (k.scan_code == 2)
            return OCBL_KEY_DOWN;    /* SCAN_DOWN */
        if (k.unicode == '\r' || k.unicode == '\n')
            return OCBL_KEY_ENTER;
        /* every other key is consumed and ignored (three-key rule) */
    }
    return -1;
}

/* ---- rendering: ConOut ---- */
static void con_out_str(const char* s)
{
    static efi_char16 w[160];
    u32 i = 0;
    while (s[i] && i + 1 < sizeof(w) / sizeof(w[0])) {
        w[i] = (efi_char16)(u8)s[i];
        i++;
    }
    w[i] = 0;
    if (ocbl_efi_st->con_out)
        ocbl_efi_st->con_out->output_string(ocbl_efi_st->con_out, w);
}

void ocbl_plat_con_init(void)
{
    if (!ocbl_efi_st->con_out)
        return;
    ocbl_efi_st->con_out->reset(ocbl_efi_st->con_out, 0);
    ocbl_efi_st->con_out->clear_screen(ocbl_efi_st->con_out);
    ocbl_efi_st->con_out->enable_cursor(ocbl_efi_st->con_out, 0);
    /* pad the ENTRY lines like the VGA renderer does (full-width
     * bar): pre-clear attribute 0x07 over the 25 rows */
    ocbl_efi_st->con_out->set_attribute(ocbl_efi_st->con_out, 0x07);
}

void ocbl_plat_con_line(u8 row, u8 col, const char* s, u8 style)
{
    if (!ocbl_efi_st->con_out)
        return;
    /* attributes: 0x07 gray-on-black, 0x0F bright, 0x70 inverse bar */
    u64 attr = 0x07;
    if (style == OCBL_CON_SELECT)
        attr = 0x70;
    else if (style == OCBL_CON_BRIGHT)
        attr = 0x0F;
    if (style == OCBL_CON_ENTRY || style == OCBL_CON_SELECT) {
        /* full-width padded bar: spaces at the attribute, then text */
        ocbl_efi_st->con_out->set_attribute(ocbl_efi_st->con_out, attr);
        ocbl_efi_st->con_out->set_cursor_pos(ocbl_efi_st->con_out,
                                             0, row);
        static efi_char16 blank[81];
        for (int i = 0; i < 80; i++)
            blank[i] = ' ';
        blank[80] = 0;
        ocbl_efi_st->con_out->output_string(ocbl_efi_st->con_out, blank);
    } else {
        ocbl_efi_st->con_out->set_attribute(ocbl_efi_st->con_out, attr);
    }
    ocbl_efi_st->con_out->set_cursor_pos(ocbl_efi_st->con_out, col, row);
    con_out_str(s);
}
