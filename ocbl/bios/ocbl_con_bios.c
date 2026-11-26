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

/* ---- clock: the PIT channel 0 read directly -----------------------
 * WP-runtime-hardening: OCRMECL flattens the machine with every PIC
 * line masked, so the BIOS tick (IRQ0 -> BDA 0x46C -> int 1Ah) never
 * advances and the old int-1Ah clock froze the menu countdown on the
 * very first post-OCRMECL full-ISO boot.  The menu now reads the PIT
 * counter itself (mode 2, 50 ms period, fractional accumulator) -
 * the same implementation the RAW flavor uses: no IRQ, no firmware
 * dependency, deterministic under the flattened machine.
 *
 * WP-compat-and-security-hardening B4: the CMOS RTC seconds
 * register is the PRIMARY clock (UIP-safe read; one second boundary
 * = exactly 1000 ms - the menu renders per second, so the
 * granularity matches the consumer).  The PIT path is the FALLBACK
 * and only runs when the RTC does not answer (RegD VRT clear).
 * The old PIT arithmetic multiplied the tick delta by 50 BEFORE
 * comparing with PIT_DIV - it counted one 50 ms period every ~1193
 * ticks (~1 ms of wall clock), so the menu's "10 second" countdown
 * expired in 0.2 s (50x fast, on QEMU AND VMware).  The fallback
 * keeps the mode-2 ch0 counter but adds the RAW delta and carries
 * 50 ms per full PIT_DIV - exactly right. */
#define PIT_DIV 59659u

static u32 g_ms;
static u32 g_sub;
static u16 g_last;
static int g_pit_ready;

static void pit_out(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static u8 pit_in(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static u16 pit_read(void)
{
    pit_out(0x43, 0x00);                       /* latch channel 0 */
    u8 lo = pit_in(0x40);
    u8 hi = pit_in(0x40);
    return (u16)((hi << 8) | lo);
}

/* ---- B4: CMOS RTC seconds anchor (UIP-safe) ---- */
static int g_rtc_usable = -1;    /* -1 = not probed yet */
static u8  g_rtc_last_sec = 0xFF;

/* Read a CMOS RTC register through ports 0x70/0x71 (NMI masked while
 * the index register is held, restored on exit). */
static u8 cmos_read(u8 idx)
{
    u8 prev = pit_in(0x70);
    pit_out(0x70, (u8)(idx | 0x80u));      /* NMI off while selecting */
    u8 v = pit_in(0x71);
    pit_out(0x70, prev & 0x7Fu);           /* restore the NMI mask bit */
    return v;
}

/* 1 when the CMOS RTC answers and its clock is valid (RegD VRT set). */
static int rtc_probe(void)
{
    if (g_rtc_usable >= 0) return g_rtc_usable;
    /* RegD bit 7 = valid RAM and time (set while the battery holds). */
    g_rtc_usable = (cmos_read(0x0D) & 0x80u) ? 1 : 0;
    if (g_rtc_usable) {
        (void)cmos_read(0x0A);             /* drain */
        g_rtc_last_sec = cmos_read(0x00);  /* anchor the first second */
    }
    return g_rtc_usable;
}

u32 ocbl_plat_ms(void)
{
    /* ---- B4 primary: the RTC seconds anchor ----------------------
     * Each second boundary crossed adds exactly 1000 ms - the menu
     * renders once per second, so the granularity matches the
     * consumer exactly.  The read is UIP-safe: while the RTC is
     * mid-update (RegA bit 7) the seconds register may be torn, so
     * the boundary is picked up on the next clean read instead. */
    if (rtc_probe()) {
        if (!(cmos_read(0x0A) & 0x80u)) {      /* UIP clear: safe */
            u8 sec = cmos_read(0x00);
            if (sec != g_rtc_last_sec) {
                g_ms += 1000u;                 /* one second boundary */
                g_rtc_last_sec = sec;
            }
        }
        return g_ms;
    }

    /* ---- B4 fallback: the PIT, with the CORRECT arithmetic -------- */
    if (!g_pit_ready) {
        pit_out(0x43, 0x34);                   /* ch0, lo/hi, mode 2 */
        pit_out(0x40, (u8)(PIT_DIV & 0xFF));
        pit_out(0x40, (u8)(PIT_DIV >> 8));
        g_last = pit_read();
        g_pit_ready = 1;
    }
    u16 now = pit_read();
    u16 delta = (u16)(g_last - now);
    g_last = now;
    g_sub += (u32)delta;                       /* raw ticks: PIT_DIV = 50 ms */
    while (g_sub >= PIT_DIV) {
        g_ms += 50;
        g_sub -= PIT_DIV;
    }
    return g_ms;
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
    /* serial console keying: read the 16550 directly (LSR bit 0 =
     * data ready).  WP-tui-input-encoding-hardening 4.6 (extended
     * sweep, live-caught): the previous int 14h AH=03/AH=02 route
     * returned "no data" forever under QEMU/SeaBIOS (its int 14h
     * implementation is minimal), so the menu was serial-dead while
     * the PS/2 path worked; the direct poll matches the EFI flavor's
     * getkey AND the serial OUTPUT path's own 0x3F8 assumption - one
     * contract everywhere. */
    u8 lsr;
    __asm__ volatile("inb %1, %0" : "=a"(lsr) : "dN"((u16)0x3FD));
    if (lsr & 0x01) {
        char c;
        __asm__ volatile("inb %1, %0" : "=a"(c) : "dN"((u16)0x3F8));
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

/* ---- WP-severe-fix-and-closure B-vmx1: the menu idle pace -----------
 *
 * The menu loop (ocbl_menu_run) spins on getkey + plat_ms with NO
 * idle gap: every iteration costs a BIOS int 16h call (a VM exit
 * into the firmware under VMware), a 16550 LSR port read (another
 * exit) and up to 8 CMOS port ops in plat_ms - an un-throttled exit
 * storm for the whole 10 s countdown.  This pure-CPU spin adds ~1-2
 * ms of ZERO-port-I/O idle between polls: the exit rate drops
 * ~1000x while key latency stays human-invisible.  OCBL has no TSC
 * calibration - a fixed spin count is deliberately approximate
 * (any post-2005 x86_64 runs it in 0.4-4 ms); the exact duration is
 * irrelevant, only the port-I/O rate matters. */
static u32 g_menu_poll_iters;   /* the before/after loop-volume evidence */

void ocbl_plat_idle_pause(void)
{
    for (volatile u32 i = 0; i < 600000u; i++)
        __asm__ volatile("pause");
}

u32 ocbl_plat_menu_poll_count(void)
{
    return g_menu_poll_iters;
}

void ocbl_plat_menu_poll_tick(void)
{
    g_menu_poll_iters++;
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
