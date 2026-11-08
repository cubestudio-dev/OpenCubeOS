/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - kernel C entry point (NAK-01-tree).
 *
 * Skeleton scope only: initialise the COM1 UART, print the four-line
 * boot banner and halt. No memory management, no interrupts, no
 * drivers - those arrive in later work packages.
 *
 * OCOS_BUILD_TIMESTAMP is provided by ocos/Makefile and derives from
 * SOURCE_DATE_EPOCH, which is what makes the banner reproducible.
 */

#include <stdint.h>
#include <stddef.h>

#ifndef OCOS_BUILD_TIMESTAMP
#define OCOS_BUILD_TIMESTAMP "unknown"
#endif

#define OCOS_VERSION "0.1.0-dev"
#define OCOS_ARCH    "x86_64"

/* COM1 - first 16550 UART at I/O port 0x3F8. */
#define COM1 0x3F8U

#define COM1_DATA (COM1 + 0U)   /* transmit / receive buffer */
#define COM1_IER  (COM1 + 1U)   /* interrupt enable (DLAB high byte) */
#define COM1_FCR  (COM1 + 2U)   /* FIFO control */
#define COM1_LCR  (COM1 + 3U)   /* line control */
#define COM1_MCR  (COM1 + 4U)   /* modem control */
#define COM1_LSR  (COM1 + 5U)   /* line status */

#define COM1_LSR_THRE 0x20U     /* transmitter holding register empty */

static inline void io_outb(uint16_t port, uint8_t value)
{
    /* %w1 forces the %dx register form (`outb %al, %dx`): the only
     * encoding every gcc/binutils combination accepts (imm8 port
     * operands do not exist above 0xFF). */
    __asm__ __volatile__("outb %b0, %w1"
                         :
                         : "a"(value), "d"(port));
}

static inline uint8_t io_inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %w1, %b0"
                         : "=a"(value)
                         : "d"(port));
    return value;
}

static void com1_init(void)
{
    io_outb(COM1_IER, 0x00U);   /* no UART interrupts */
    io_outb(COM1_LCR, 0x80U);   /* DLAB on */
    io_outb(COM1_DATA, 0x01U);  /* divisor 1 -> 115200 baud */
    io_outb(COM1_IER, 0x00U);
    io_outb(COM1_LCR, 0x03U);   /* 8 data bits, no parity, DLAB off */
    io_outb(COM1_FCR, 0xC7U);   /* FIFO on, cleared, 14-byte trigger */
    io_outb(COM1_MCR, 0x0BU);   /* DTR + RTS + OUT2 */
}

static void com1_putc(char c)
{
    while ((io_inb(COM1_LSR) & COM1_LSR_THRE) == 0U) {
        /* wait until the transmitter holding register is empty */
    }
    io_outb(COM1_DATA, (uint8_t)c);
}

static void com1_puts(const char *s)
{
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        com1_putc(s[i]);
    }
}

void kmain(uint32_t mb2_magic, uint32_t mb2_info_addr)
{
    (void)mb2_magic;
    (void)mb2_info_addr;

    com1_init();

    com1_puts("Open Cube OS NAK kernel booted\n");
    com1_puts("Version: " OCOS_VERSION "\n");
    com1_puts("Arch: " OCOS_ARCH "\n");
    com1_puts("Build: " OCOS_BUILD_TIMESTAMP "\n");

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
