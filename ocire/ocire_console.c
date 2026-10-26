/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_console.c
 * Purpose: OCIRE dual console (serial + VGA text).
 *
 * Serial: 16550 UART at COM1 (0x3F8), 115200 8N1, polled - OCIRE is a
 *         polling kernel by design (no external interrupt controller
 *         setup is needed for a recovery shell).
 * VGA:    80x25 text buffer at physical 0xB8000, white-on-black, with
 *         hardware cursor tracking through ports 0x3D4/0x3D5.
 */

#include "ocire_console.h"
#include "ocire_io.h"
#include "ocire_util.h"

#define COM1 0x3F8

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_BASE ((volatile u16 *)0xB8000)

static int g_row;
static int g_col;

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

/* ---- VGA text ---- */

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

/* ---- public console API ---- */

void ocire_console_init(void) {
    serial_init();
    g_row = 0;
    g_col = 0;
    for (int y = 0; y < VGA_ROWS; y++) {
        for (int x = 0; x < VGA_COLS; x++)
            VGA_BASE[(u64)y * VGA_COLS + x] = (u16)0x0F00;
    }
    vga_update_cursor();
}

void ocire_putc(char c) {
    serial_putc(c);
    vga_putc(c);
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
