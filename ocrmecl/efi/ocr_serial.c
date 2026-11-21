/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocrmecl/efi/ocr_serial.c
 * Purpose: 16550 COM1 serial console for the OCRMECL EFI track -
 *          polled, 115200 8N1, the same line discipline every other
 *          boot stage uses, so the evidence transcript stays one
 *          continuous record: OVMF -> OCRMECL -> OCBL -> kernel.
 *
 *          Own copy on purpose: OCRMECL shares no object with OCBL
 *          (physical separation, interface/boot_handoff.h is the
 *          only contract).
 */
#include "ocr_efi.h"

#define COM1 0x3F8

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

void ocr_serial_init(void)
{
    outb_(COM1 + 1, 0x00);   /* no UART interrupts                */
    outb_(COM1 + 3, 0x80);   /* DLAB on                           */
    outb_(COM1 + 0, 0x01);   /* divisor 1 = 115200 baud           */
    outb_(COM1 + 1, 0x00);
    outb_(COM1 + 3, 0x03);   /* 8N1, DLAB off                     */
    outb_(COM1 + 2, 0xC7);   /* FIFO enable + clear               */
    outb_(COM1 + 4, 0x0B);   /* DTR | RTS | OUT2                  */
}

static void putc_(char c)
{
    while (!(inb_(COM1 + 5) & 0x20))
        ;
    outb_(COM1, (u8)c);
    if (c == '\n')
        putc_('\r');
}

void ocr_serial_puts(const char* s)
{
    while (*s)
        putc_(*s++);
}

void ocr_serial_hex(u64 v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    if (digits <= 0) {
        digits = 16;
        while (digits > 1 && !(v >> ((digits - 1) * 4)))
            digits--;
    }
    ocr_serial_puts("0x");
    for (int i = digits - 1; i >= 0; i--)
        putc_(hex[(v >> (i * 4)) & 0xF]);
}

void ocr_serial_dec(u64 v)
{
    char b[24];
    int i = 0;
    if (!v) {
        putc_('0');
        return;
    }
    while (v && i < 24) {
        b[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--)
        putc_(b[i]);
}
