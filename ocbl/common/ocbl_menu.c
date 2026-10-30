/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_menu.c
 * Purpose: the boot menu state machine (see ocbl_menu.h).
 *
 * Layout (80x25):
 *   row 0  title
 *   row 1  hint
 *   row 2  countdown / armed hint
 *   row 3  blank
 *   row 4..22  the entry window (19 rows, scrolled)
 *
 * The Shutdown row is appended to the scanned entries and can never
 * be scrolled out of existence - it is the last line and the window
 * clamps so it stays reachable.
 *
 * Serial keys: the harness drives selection by writing to the serial
 * line (QEMU -serial stdio stdin -> 16550 -> int 14h).  A CSI arrow
 * arrives as ESC '[' 'A'/'B'; ESC and '[' fall through the ignore
 * rule, the letter lands as Up/Down.  \r / \n are Enter.
 */
#include "ocbl_menu.h"
#include "ocbl_scan.h"
#include "ocbl_serial.h"
#include "ocbl_vga.h"
#include "ocbl_bios.h"

#define OCBL_MENU_WINDOW  19

#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_ENTER 0x1C

#ifndef OCBL_STRINGIFY
#define OCBL_STRINGIFY_(x) #x
#define OCBL_STRINGIFY(x) OCBL_STRINGIFY_(x)
#endif

static const char shutdown_label[] = "Shutdown";

static u32 total_entries;               /* scanned + shutdown */
static u32 sel, top;

/* ---- clock: int 1Ah AH=00h ticks since midnight (54.9 ms) ---- */
static u32 ticks_now(void)
{
    ocbl_bios_regs_t r = {0};
    r.eax = 0x0000;
    ocbl_bios_int(0x1A, &r);
    return ((u32)r.ecx << 16) | (r.edx & 0xFFFF);
}

/* ---- keyboard: int 16h (PS/2) + int 14h (serial line) ----
 * Both sources collapse to the same three-key contract. */
static int poll_key(void)
{
    ocbl_bios_regs_t r = {0};
    r.eax = 0x0100;                     /* AH=01: peek, ZF=0 if ready */
    ocbl_bios_int(0x16, &r);
    if (!(r.flags & 0x40)) {
        r.eax = 0x0000;                 /* AH=00: consume */
        ocbl_bios_int(0x16, &r);
        u16 k = (u16)(r.eax & 0xFFFF);
        u8 sc = (u8)(k >> 8);
        if (sc == KEY_UP || sc == KEY_DOWN || sc == KEY_ENTER)
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
            return KEY_UP;              /* CSI tail after ESC+['[' */
        if (c == 'B')
            return KEY_DOWN;
        if (c == '\r' || c == '\n')
            return KEY_ENTER;
        return -1;
    }
    return -1;
}

/* ---- rendering ---- */
static void serial_at(u8 row, u8 col, const char* s, u8 inverse)
{
    char esc[24];
    u32 i = 0;
    esc[i++] = 0x1B;
    esc[i++] = '[';
    if (row >= 10)
        esc[i++] = (char)('0' + row / 10);
    esc[i++] = (char)('0' + row % 10);
    esc[i++] = ';';
    if (col >= 10)
        esc[i++] = (char)('0' + col / 10);
    esc[i++] = (char)('0' + col % 10);
    esc[i++] = 'H';
    esc[i] = 0;
    if (inverse)
        ocbl_serial_puts("\x1B[7m");
    ocbl_serial_puts(esc);
    ocbl_serial_puts(s);
    if (inverse)
        ocbl_serial_puts("\x1B[0m");
}

static void line_at(u8 row, const char* label, u8 selected)
{
#ifdef OCBL_FLAVOR_BIOS
    u8 attr = selected ? OCBL_VGA_ATTR_SELECT : OCBL_VGA_ATTR_NORMAL;
    ocbl_vga_padline(row, attr);
    ocbl_vga_puts(row, 1, attr, label);
#endif
    serial_at(row, 1, label, selected);
}

static void put_dec(char** p, u32 v)
{
    char b[12];
    u32 i = 0;
    do {
        b[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i)
        *(*p)++ = b[--i];
}

static void render_full(void)
{
#ifdef OCBL_FLAVOR_BIOS
    ocbl_vga_text_mode();
    ocbl_vga_clear();
    ocbl_vga_cursor_off();
#endif
    {
        char t[96];
        char* p = t;
        static const char h1[] =
            "Open Cube OS Boot Loader (" OCBL_RELEASE " build ";
        for (u32 k = 0; h1[k]; k++)
            *p++ = h1[k];
        put_dec(&p, OCBL_BUILD);
        *p++ = ')';
        *p = 0;
#ifdef OCBL_FLAVOR_BIOS
        ocbl_vga_puts(0, 1, OCBL_VGA_ATTR_BRIGHT, t);
#endif
        serial_at(0, 1, t, 0);
    }
#ifdef OCBL_FLAVOR_BIOS
    ocbl_vga_puts(1, 1, OCBL_VGA_ATTR_NORMAL,
                  "Up/Down to select, Enter to boot. Other keys are not accepted.");
#endif
    serial_at(1, 1, "Up/Down to select, Enter to boot.", 0);

    ocbl_scan_t* t = ocbl_scan_result();
    for (u32 r = 0; r < OCBL_MENU_WINDOW; r++) {
        u32 i = top + r;
        if (i >= total_entries)
            break;
        if (i == t->count)
            line_at((u8)(4 + r), shutdown_label, i == sel);
        else
            line_at((u8)(4 + r), t->entries[i].label, i == sel);
    }
}

/* countdown line: "Booting <label> in Ns" or the armed hint */
static void render_countdown(u32 remain, u32 countdown_active)
{
    static char line[96];
    u32 o = 0;
    static const char pre[] = "Booting ";
    for (u32 k = 0; pre[k]; k++)
        line[o++] = pre[k];
    ocbl_scan_t* t = ocbl_scan_result();
    const char* lab = sel == t->count ? shutdown_label
                                      : t->entries[sel].label;
    for (u32 k = 0; lab[k] && o < 64; k++)
        line[o++] = lab[k];
    if (countdown_active) {
        static const char mid[] = " in ";
        for (u32 k = 0; mid[k]; k++)
            line[o++] = mid[k];
        char* p = line + o;
        put_dec(&p, remain);
        o = (u32)(p - line);
        line[o++] = 's';
    } else {
        static const char mid[] = " (timer cancelled)";
        for (u32 k = 0; mid[k]; k++)
            line[o++] = mid[k];
    }
    while (o < 78)
        line[o++] = ' ';
    line[o] = 0;
#ifdef OCBL_FLAVOR_BIOS
    ocbl_vga_puts(2, 1, OCBL_VGA_ATTR_BRIGHT, line);
#endif
    serial_at(2, 1, line, 0);
}

static void clamp_window(void)
{
    if (sel < top)
        top = sel;
    if (sel >= OCBL_MENU_WINDOW)
        top = sel - OCBL_MENU_WINDOW + 1;
    if (top + OCBL_MENU_WINDOW > total_entries) {
        top = total_entries > OCBL_MENU_WINDOW
                  ? total_entries - OCBL_MENU_WINDOW : 0;
    }
}

u32 ocbl_menu_run(void)
{
    ocbl_scan_t* t = ocbl_scan_result();
    total_entries = t->count + 1;       /* + the Shutdown row */

    /* default = first OCOS, else the first scanned entry; with no
     * entries at all the Shutdown row is the only (and default) one */
    sel = total_entries - 1;            /* shutdown fallback */
    for (u32 i = 0; i < t->count; i++) {
        if (t->entries[i].type == OCBL_ENT_OCOS) {
            sel = i;
            break;
        }
    }
    if (sel == total_entries - 1 && t->count)
        sel = 0;
    top = 0;
    clamp_window();

    render_full();
    render_countdown(OCBL_MENU_TIMEOUT_SEC, 1);


    u32 base = ticks_now();
    u32 last_shown = 0;
    u32 cancelled = 0;
    for (;;) {
        int k = poll_key();
        if (k == KEY_UP || k == KEY_DOWN) {
            if (k == KEY_UP)
                sel = sel ? sel - 1 : total_entries - 1;
            else
                sel = (sel + 1) % total_entries;
            clamp_window();
            render_full();
            render_countdown(0, 0);     /* cancel the timer */
            cancelled = 1;
            continue;
        }
        if (k == KEY_ENTER)
            return sel;

        if (cancelled)
            continue;                   /* armed mode: no timeout */
        u32 dt = (ticks_now() - base) / 18;
        if (dt >= OCBL_MENU_TIMEOUT_SEC)
            return sel;                 /* default boots */
        if (dt != last_shown) {
            last_shown = dt;
            render_countdown(OCBL_MENU_TIMEOUT_SEC - dt, 1);
        }
    }
}

void ocbl_menu_announce(u32 index)
{
    ocbl_scan_t* t = ocbl_scan_result();
    ocbl_serial_puts("[menu] selected: ");
    if (index >= t->count) {
        ocbl_serial_puts(shutdown_label);
        ocbl_serial_puts("\r\n[menu] shutdown request (S5 wiring lands in B4)\r\n");
        return;
    }
    ocbl_serial_puts(t->entries[index].label);
    ocbl_serial_puts("\r\n[menu] B4 will load this payload (mb2 handoff)\r\n");
}
