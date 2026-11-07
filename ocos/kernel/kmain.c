/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - kernel entry stub (NAK-01-tree).
 *
 * Minimal freestanding kmain(): prints "NAK kernel stub" to the COM1
 * serial port through inline assembly (direct port I/O, no drivers
 * exist yet), then parks the CPU. The ELF only has to link at this
 * stage; real kernel work starts in later work packages. Nothing in
 * this file fakes behaviour beyond the printed stub line.
 */

#include <stdint.h>

#define COM1_DATA 0x3F8

static void com1_putc(char c)
{
    /* %w1 forces the 16-bit %dx register form (`outb %al, %dx`), the
     * only encoding accepted by every gcc/binutils combination; an
     * immediate port operand would be rejected because 0x3F8 exceeds
     * the 8-bit imm8 port range. */
    __asm__ __volatile__("outb %b0, %w1" : : "a"(c), "d"((uint16_t)COM1_DATA));
}

static void com1_puts(const char *s)
{
    while (*s != '\0') {
        com1_putc(*s);
        s++;
    }
}

void kmain(void)
{
    com1_puts("NAK kernel stub\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
