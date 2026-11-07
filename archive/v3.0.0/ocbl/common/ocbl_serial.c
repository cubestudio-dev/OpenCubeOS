/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_serial.c
 * Purpose: 16550 COM1 serial console, polled, 115200 8N1 - the primary
 *          OCBL output channel (mirrors the menu text, test harness
 *          input/output, and every diagnostic).
 *
 * stage1 already programmed the UART on the BIOS path; ocbl_serial_init
 * is idempotent and runs anyway (the UEFI path needs it too).
 */
#include "ocbl_serial.h"

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

void ocbl_serial_init(void)
{
    outb_(COM1 + 1, 0x00);   /* no UART interrupts                 */
    outb_(COM1 + 3, 0x80);   /* DLAB on                            */
    outb_(COM1 + 0, 0x01);   /* divisor 1 = 115200 baud            */
    outb_(COM1 + 1, 0x00);
    outb_(COM1 + 3, 0x03);   /* 8N1, DLAB off                      */
    outb_(COM1 + 2, 0xC7);   /* FIFO enable + clear                */
    outb_(COM1 + 4, 0x0B);   /* DTR | RTS | OUT2                   */
}

void ocbl_serial_putc(char c)
{
    while (!(inb_(COM1 + 5) & 0x20))
        ;                     /* THR empty */
    outb_(COM1, (u8)c);
    if (c == '\n')
        ocbl_serial_putc('\r');
}

void ocbl_serial_puts(const char* s)
{
    while (*s)
        ocbl_serial_putc(*s++);
}

void ocbl_serial_hex(u64 v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    if (digits <= 0 || digits > 16) {
        digits = 16;
        while (digits > 1 && !((v >> ((digits - 1) * 4)) & 0xF))
            digits--;
    }
    for (i = digits - 1; i >= 0; i--)
        ocbl_serial_putc(hex[(v >> (i * 4)) & 0xF]);
}

void ocbl_serial_dec(u64 v)
{
    char buf[24];
    int i = 0;
    if (v == 0) {
        ocbl_serial_putc('0');
        return;
    }
    while (v && i < 24) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        ocbl_serial_putc(buf[--i]);
}
