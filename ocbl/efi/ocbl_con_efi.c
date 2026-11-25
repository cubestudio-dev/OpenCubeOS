/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocbl/efi/ocbl_con_efi.c
 * Purpose: the RAW flavor of the platform console/clock/keyboard
 *          backend (ocbl_platform.h).  Since WP-runtime-hardening the
 *          UEFI track enters OCBL AFTER ExitBootServices (through
 *          OCRMECL, interface/boot_handoff.h) - no ConOut, no timer
 *          events, no ReadKeyStroke exist anymore.  The backend is
 *          therefore pure hardware:
 *
 *            console - the GOP framebuffer captured by OCRMECL, with
 *                      the 8x16 ASCII font (ocbl_font_data.c); the
 *                      80x25 menu grid renders at 8x16 cell size
 *            clock   - the PIT channel 0, reprogrammed to mode 2
 *                      (divisor 59659 = 50 ms period, polled with a
 *                      fractional accumulator; the kernel reprograms
 *                      the PIT during its own bring-up anyway)
 *            keys    - the 16550 serial line only (the same
 *                      three-key contract as the BIOS flavor's
 *                      direct-poll path; 'A'/'B' = CSI arrow tails)
 *
 *          The serial RENDER of the menu stays in common code
 *          (ocbl_menu.c) - unchanged.
 */
#include "../common/ocbl_platform.h"
#include "../common/ocbl_serial.h"
#include "ocbl_font.h"
#include "boot_handoff.h"

/* ---- framebuffer state (set once from the OCRMECL handoff) ---- */
static u8* g_fb;
static u32 g_pitch, g_w, g_h;
static u8 g_bgr;

void ocbl_fbcon_init(const ocr_uefi_handoff_t* h)
{
    g_fb = (u8*)(u64)h->fb_addr;
    g_pitch = h->fb_pitch;
    g_w = h->fb_width;
    g_h = h->fb_height;
    g_bgr = !h->fb_rgb;             /* 1 = BGRx (pixel format 1) */
    if (!g_fb)
        return;
    for (u32 y = 0; y < g_h; y++) {
        u32* row = (u32*)(g_fb + (u64)y * g_pitch);
        for (u32 x = 0; x < g_w && x * 4 < g_pitch * 8; x++)
            row[x] = 0xFF000000;
    }
}

static void put_glyph(u32 px, u32 py, char c, u32 fg, u32 bg)
{
    if (!g_fb || px + 8 > g_w || py + 16 > g_h)
        return;
    const u8* glyph = ocbl_font8x16[(u8)c - 32];
    for (u32 y = 0; y < 16; y++) {
        u8 bits = glyph[y];
        u32* row = (u32*)(g_fb + (u64)(py + y) * g_pitch);
        for (u32 x = 0; x < 8; x++) {
            u32 v = (bits & (0x80u >> x)) ? fg : bg;
            /* BGRA vs RGBA byte order in the 32-bit word: the QEMU
             * std-VGA GOP exposes format 1 (BGRx: B@16 G@8 R@0) or
             * format 0 (RGBx: R@16 G@8 B@0); fold via the flag. */
            if (g_bgr)
                row[px + x] = 0xFF000000u |
                    ((v & 0xFF) << 16) | (v & 0x00FF00) | ((v >> 16) & 0xFF);
            else
                row[px + x] = 0xFF000000u | v;
        }
    }
}

/* ---- the ocbl_platform.h console contract (80x25 grid) ---- */
void ocbl_plat_con_init(void)
{
    if (!g_fb)
        return;
    for (u32 y = 0; y < g_h; y++) {
        u32* row = (u32*)(g_fb + (u64)y * g_pitch);
        for (u32 x = 0; x < g_w; x++)
            row[x] = 0xFF000000;
    }
}

void ocbl_plat_con_line(u8 row, u8 col, const char* s, u8 style)
{
    if (!g_fb)
        return;
    u32 fg = 0xCCCCCC, bg = 0x000000;
    if (style == OCBL_CON_SELECT) {
        fg = 0x000000;
        bg = 0xCCCCCC;
    } else if (style == OCBL_CON_BRIGHT) {
        fg = 0xFFFFFF;
    }
    u32 py = (u32)row * 16;
    /* the entry/select rows get the full-width padded bar (B3 look) */
    if (style == OCBL_CON_ENTRY || style == OCBL_CON_SELECT) {
        for (u8 i = 0; i < 80; i++)
            put_glyph((u32)i * 8, py, ' ', fg, bg);
    }
    for (int i = 0; s[i] && col + i < 80; i++)
        put_glyph((u32)(col + i) * 8, py, s[i], fg, bg);
}

/* ---- clock: the CMOS RTC first, the PIT counter as fallback ------
 * WP-compat-and-security-hardening B4: the CMOS RTC seconds register
 * is the PRIMARY clock (UIP-safe read; one second boundary = exactly
 * 1000 ms - the menu renders per second, so the granularity matches
 * the consumer); the PIT path is the FALLBACK and only runs when the
 * RTC does not answer (RegD VRT clear).  The old PIT arithmetic
 * multiplied the tick delta by 50 BEFORE comparing with PIT_DIV - it
 * counted one 50 ms period every ~1193 ticks (~1 ms of wall clock),
 * so the menu's "10 second" countdown expired in 0.2 s (50x fast, on
 * QEMU AND VMware).  The fallback keeps the mode-2 ch0 counter but
 * adds the RAW delta and carries 50 ms per full PIT_DIV - exactly
 * right.
 * The counter counts DOWN at 1193182 Hz and reloads at the divisor;
 * elapsed ticks between two reads = last - now (mod 2^16). */
#define PIT_DIV 59659u

static u32 g_ms;
static u32 g_sub;
static u16 g_last;
static int g_pit_ready;

static void outb_(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static u8 inb_(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static u16 pit_read(void)
{
    outb_(0x43, 0x00);              /* latch channel 0 */
    u8 lo = inb_(0x40);
    u8 hi = inb_(0x40);
    return (u16)((hi << 8) | lo);
}

static void pit_ensure(void)
{
    if (g_pit_ready)
        return;
    outb_(0x43, 0x34);              /* ch0, lo/hi, mode 2 */
    outb_(0x40, (u8)(PIT_DIV & 0xFF));
    outb_(0x40, (u8)(PIT_DIV >> 8));
    g_last = pit_read();
    g_pit_ready = 1;
}

/* ---- B4: CMOS RTC seconds anchor (UIP-safe) ---- */
static int g_rtc_usable = -1;    /* -1 = not probed yet */
static u8  g_rtc_last_sec = 0xFF;

/* Read a CMOS RTC register through ports 0x70/0x71 (the x64 flavor
 * reads the RTC exactly like the BIOS flavor - port I/O is legal in
 * the EFI boot environment; NMI masked while the index is held). */
static u8 cmos_read(u8 idx)
{
    u8 prev = inb_(0x70);
    outb_(0x70, (u8)(idx | 0x80u));      /* NMI off while selecting */
    u8 v = inb_(0x71);
    outb_(0x70, prev & 0x7Fu);           /* restore the NMI mask bit */
    return v;
}

/* 1 when the CMOS RTC answers and its clock is valid (RegD VRT set). */
static int rtc_probe(void)
{
    if (g_rtc_usable >= 0) return g_rtc_usable;
    g_rtc_usable = (cmos_read(0x0D) & 0x80u) ? 1 : 0;
    if (g_rtc_usable) {
        (void)cmos_read(0x0A);             /* drain */
        g_rtc_last_sec = cmos_read(0x00);  /* anchor the first second */
    }
    return g_rtc_usable;
}

u32 ocbl_plat_ms(void)
{
    /* ---- B4 primary: the RTC seconds anchor (UIP-safe) ---------- */
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

    /* ---- B4 fallback: the PIT, with the CORRECT arithmetic ------ */
    pit_ensure();
    u16 now = pit_read();
    u16 delta = (u16)(g_last - now);
    g_last = now;
    g_sub += (u32)delta;               /* raw ticks: PIT_DIV = 50 ms */
    while (g_sub >= PIT_DIV) {
        g_ms += 50;
        g_sub -= PIT_DIV;
    }
    return g_ms;
}

/* ---- keys: the 16550 serial line only (post-EBS there is no other
 *      input source; same contract + CSI tails as the BIOS flavor) */
int ocbl_plat_getkey(void)
{
    u8 lsr;
    __asm__ volatile("inb %1, %0" : "=a"(lsr) : "dN"((u16)0x3FD));
    if (lsr & 0x01) {
        char c;
        __asm__ volatile("inb %1, %0" : "=a"(c) : "dN"((u16)0x3F8));
        if (c == 'A')
            return OCBL_KEY_UP;
        if (c == 'B')
            return OCBL_KEY_DOWN;
        if (c == '\r' || c == '\n')
            return OCBL_KEY_ENTER;
        return -1;
    }
    return -1;
}
