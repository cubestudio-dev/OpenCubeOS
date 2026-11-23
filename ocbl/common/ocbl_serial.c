/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_serial.c
 * Purpose: 16550 COM1 serial console, polled, 115200 8N1 - the primary
 *          OCBL output channel (mirrors the menu text, test harness
 *          input/output, and every diagnostic).
 *
 *          WP-boot-stack-vmware-and-nak-rebase F1/F2 discipline: the
 *          line is probed (scratch register write-back) at init and
 *          every THR-empty spin is BOUNDED. A blocked/dead line (the
 *          LSR never setting bit 5 - exactly the QEMU-nanny behavior
 *          real firmware does NOT guarantee) burns its budget once,
 *          is declared dead, and every later print returns - a broken
 *          serial console must never hang the boot loader.
 *
 * stage1 already programmed the UART on the BIOS path; ocbl_serial_init
 * is idempotent and runs anyway (the UEFI path needs it too).
 */
#include "ocbl_serial.h"

#define COM1 0x3F8

/* F1 spin budget: ~2M port reads (~a few hundred ms worst case on
 * real hardware) before the line is declared dead. Generous enough
 * for the slowest 16550 FIFO drain, short enough to never look like
 * a hang. */
#define OCBL_SER_SPIN_BUDGET 2000000u

static u8 g_serial_dead; /* 1 = COM1 declared dead (probe or timeout) */

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
    /* F2: scratch-register probe (two patterns). A COM1 that does not
     * hold a written SCR value is missing/broken -> declare the line
     * dead and skip every later print; the F1 timeout stays as the
     * runtime backstop for a line that probes fine but stalls later. */
    outb_(COM1 + 7, 0x5A);
    if (inb_(COM1 + 7) != 0x5A)
        goto dead;
    outb_(COM1 + 7, 0xA5);
    if (inb_(COM1 + 7) != 0xA5)
        goto dead;

    outb_(COM1 + 1, 0x00);   /* no UART interrupts                 */
    outb_(COM1 + 3, 0x80);   /* DLAB on                            */
    outb_(COM1 + 0, 0x01);   /* divisor 1 = 115200 baud            */
    outb_(COM1 + 1, 0x00);
    outb_(COM1 + 3, 0x03);   /* 8N1, DLAB off                      */
    outb_(COM1 + 2, 0xC7);   /* FIFO enable + clear                */
    outb_(COM1 + 4, 0x0B);   /* DTR | RTS | OUT2                   */
    g_serial_dead = 0;
    return;
dead:
    g_serial_dead = 1;
}

void ocbl_serial_putc(char c)
{
    u32 spins = 0;
    if (g_serial_dead)
        return;
    while (!(inb_(COM1 + 5) & 0x20)) {
        if (++spins > OCBL_SER_SPIN_BUDGET) {
            g_serial_dead = 1;   /* F1: dead line, drop the char   */
            return;
        }
    }
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
