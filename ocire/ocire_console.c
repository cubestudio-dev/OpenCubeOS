/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_console.c
 * Purpose: OCIRE dual console (serial + video).
 *
 * Serial: 16550 UART at COM1 (0x3F8), 115200 8N1, polled - OCIRE is a
 *         polling kernel by design (no external interrupt controller
 *         setup is needed for a recovery shell).
 *
 * Video (WP-02): TWO backends behind the same putchar API -
 *   LFB  - when the multiboot2 info carries an RGB 32-bpp framebuffer
 *          tag (the OCIRE header requests 800x600x32), text is drawn
 *          with the 8x16 font, 100 columns x 37 rows, with software
 *          scrolling.  This is the mode the boot supervisor hands to
 *          OCOS at kexec time, so keeping it is what makes the
 *          hand-over seamless.
 *   VGA  - the WP-01 80x25 text buffer at 0xB8000, used when no fb
 *          tag arrived (bare metal text mode, odd firmware).
 *
 * Both halves receive every character; the serial half is what
 * headless QEMU runs and pexpect tests drive.
 */

#include "ocire_console.h"
#include "ocire_io.h"
#include "ocire_util.h"
#include "ocire_mb2.h"
#include "ocire_font.h"

#define COM1 0x3F8

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_BASE ((volatile u16 *)0xB8000)

static int g_row;
static int g_col;

/* LFB state (valid when g_lfb != 0). */
static volatile u32 *g_lfb;
static int g_lfb_active;
static int g_fb_cols, g_fb_rows, g_fb_pitch_u32;

/* Text colour: white (0x00FFFFFF) on black, like the VGA 0x0F
 * attribute of the text backend. */
#define LFB_FG 0x00FFFFFFu
#define LFB_BG 0x00000000u

/* ---- serial ---- */

static void serial_init(void) {
    oc_outb(COM1 + 1, 0x00);    /* disable interrupts */
    oc_outb(COM1 + 3, 0x80);    /* DLAB on */
    oc_outb(COM1 + 0, 0x01);    /* divisor low: 115200 baud */
    oc_outb(COM1 + 1, 0x00);    /* divisor high */
    oc_outb(COM1 + 3, 0x03);    /* 8N1 */
    oc_outb(COM1 + 2, 0xC7);    /* FIFO enable + clear */
    oc_outb(COM1 + 4, 0x0B);    /* DTR+RTS+OUT2 */
}

static void serial_putc(char c) {
    /* Wait for the transmitter holding register to drain. */
    for (int i = 0; i < 100000; i++) {
        if (oc_inb(COM1 + 5) & 0x20) break;
    }
    oc_outb(COM1 + 0, (u8)c);
}

int ocire_serial_rx_ready(void) {
    return (oc_inb(COM1 + 5) & 0x01) ? 1 : 0;
}

char ocire_serial_rx(void) {
    return (char)oc_inb(COM1 + 0);
}

/* ---- VGA text backend ---- */

static void vga_update_cursor(void) {
    u16 pos = (u16)(g_row * VGA_COLS + g_col);
    oc_outb(0x3D4, 0x0F);
    oc_outb(0x3D5, (u8)(pos & 0xFF));
    oc_outb(0x3D4, 0x0E);
    oc_outb(0x3D5, (u8)(pos >> 8));
}

static void vga_scroll(void) {
    oc_memcpy((void *)VGA_BASE, (const void *)(VGA_BASE + VGA_COLS),
              (u64)(VGA_COLS * (VGA_ROWS - 1)));
    volatile u16 *last = VGA_BASE + (u64)VGA_COLS * (VGA_ROWS - 1);
    for (int i = 0; i < VGA_COLS; i++) last[i] = (u16)0x0F00;
    g_row = VGA_ROWS - 1;
    g_col = 0;
}

static void vga_putc(char c) {
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            VGA_BASE[(u64)g_row * VGA_COLS + g_col] = (u16)0x0F00;
        }
    } else {
        VGA_BASE[(u64)g_row * VGA_COLS + g_col] =
            (u16)(0x0F00 | (u8)c);
        g_col++;
        if (g_col >= VGA_COLS) { g_col = 0; g_row++; }
    }
    if (g_row >= VGA_ROWS) vga_scroll();
    vga_update_cursor();
}

/* ---- LFB backend ---- */

static void lfb_fill_cell(int row, int col, u32 color) {
    volatile u32 *dst = g_lfb + (u64)row * OCIRE_FONT_HEIGHT * g_fb_pitch_u32
                        + (u64)col * OCIRE_FONT_WIDTH;
    for (int y = 0; y < OCIRE_FONT_HEIGHT; y++) {
        volatile u32 *px = dst + (u64)y * g_fb_pitch_u32;
        for (int x = 0; x < OCIRE_FONT_WIDTH; x++) px[x] = color;
    }
}

static void lfb_draw_char(int row, int col, char c) {
    const u8 *glyph = ocire_font8x16[(u8)c];
    volatile u32 *dst = g_lfb + (u64)row * OCIRE_FONT_HEIGHT * g_fb_pitch_u32
                        + (u64)col * OCIRE_FONT_WIDTH;
    for (int y = 0; y < OCIRE_FONT_HEIGHT; y++) {
        u8 bits = glyph[y];
        volatile u32 *px = dst + (u64)y * g_fb_pitch_u32;
        for (int x = 0; x < OCIRE_FONT_WIDTH; x++) {
            px[x] = (bits & (0x80 >> x)) ? LFB_FG : LFB_BG;
        }
    }
}

static void lfb_scroll(void) {
    /* Move rows 1..N-1 up by one text row (FONT_H pixel lines). */
    u64 move = (u64)(g_fb_rows - 1) * OCIRE_FONT_HEIGHT * g_fb_pitch_u32;
    oc_memcpy((void *)g_lfb,
              (const void *)(g_lfb + (u64)OCIRE_FONT_HEIGHT * g_fb_pitch_u32),
              move * 4);
    /* Clear the last text row. */
    volatile u32 *last = g_lfb +
        (u64)(g_fb_rows - 1) * OCIRE_FONT_HEIGHT * g_fb_pitch_u32;
    for (int y = 0; y < OCIRE_FONT_HEIGHT; y++) {
        volatile u32 *px = last + (u64)y * g_fb_pitch_u32;
        for (int x = 0; x < (g_fb_cols * OCIRE_FONT_WIDTH) && x < g_fb_pitch_u32; x++)
            px[x] = LFB_BG;
    }
    g_row = g_fb_rows - 1;
    g_col = 0;
}

static void lfb_putc(char c) {
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            lfb_fill_cell(g_row, g_col, LFB_BG);
        }
    } else {
        lfb_draw_char(g_row, g_col, c);
        g_col++;
        if (g_col >= g_fb_cols) { g_col = 0; g_row++; }
    }
    if (g_row >= g_fb_rows) lfb_scroll();
}

/* ---- public console API ---- */

void ocire_console_init(void) {
    serial_init();
    g_row = 0;
    g_col = 0;
    g_lfb_active = 0;

    /* Prefer the multiboot2 RGB framebuffer (WP-02 LFB console). */
    const ocire_mb2_info_t *mb = ocire_mb2_get();
    if (mb->fb && mb->fb->fb_type == 1 && mb->fb->bpp == 32 &&
        mb->fb->addr != 0 && mb->fb->width >= 320 && mb->fb->height >= 200 &&
        mb->fb->pitch >= mb->fb->width * 4) {
        g_lfb = (volatile u32 *)(uintptr)mb->fb->addr;
        g_fb_pitch_u32 = (int)(mb->fb->pitch / 4);
        g_fb_cols = (int)(mb->fb->width / OCIRE_FONT_WIDTH);
        g_fb_rows = (int)(mb->fb->height / OCIRE_FONT_HEIGHT);
        if (g_fb_cols < 1) g_fb_cols = 1;
        if (g_fb_rows < 1) g_fb_rows = 1;
        g_lfb_active = 1;
        for (int y = 0; y < g_fb_rows; y++)
            for (int x = 0; x < g_fb_cols; x++)
                lfb_fill_cell(y, x, LFB_BG);
        return;
    }

    /* VGA text fallback (WP-01 path). */
    for (int y = 0; y < VGA_ROWS; y++) {
        for (int x = 0; x < VGA_COLS; x++)
            VGA_BASE[(u64)y * VGA_COLS + x] = (u16)0x0F00;
    }
    vga_update_cursor();
}

int ocire_console_is_lfb(void) {
    return g_lfb_active;
}

void ocire_putc(char c) {
    serial_putc(c);
    if (g_lfb_active) lfb_putc(c);
    else              vga_putc(c);
}

void ocire_puts(const char *s) {
    if (!s) return;
    while (*s) ocire_putc(*s++);
}

void ocire_puthex(const char *label, u64 value, int nibbles) {
    static const char hex[] = "0123456789ABCDEF";
    ocire_puts(label);
    if (nibbles < 1) nibbles = 1;
    if (nibbles > 16) nibbles = 16;
    for (int i = nibbles - 1; i >= 0; i--) {
        ocire_putc(hex[(value >> (i * 4)) & 0xF]);
    }
}

void ocire_putdec(const char *label, u64 value) {
    char buf[24];
    u64 i = 0;
    ocire_puts(label);
    if (value == 0) { ocire_putc('0'); return; }
    while (value && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i--) ocire_putc(buf[i]);
}
