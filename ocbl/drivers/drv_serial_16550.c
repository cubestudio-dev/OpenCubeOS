/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL COM1 16550 UART driver (WP-02-ocbl).
 *
 * Implementation for the interface declared in drv_serial_16550.h.
 * Shared verbatim by the BIOS path (gcc ELF64) and the UEFI path
 * (mingw PE32+) - the boot log must be byte-identical no matter
 * which firmware brought the machine up.
 */

#include <stdint.h>
#include <stddef.h>

#include "lib/core_io_port.h"
#include "drivers/drv_serial_16550.h"

/* COM1 - first 16550 UART at I/O port base 0x3F8. */
#define DRV_SERIAL_COM1_BASE 0x3F8U

#define DRV_SERIAL_REG_DATA (DRV_SERIAL_COM1_BASE + 0U) /* transmit / receive buffer */
#define DRV_SERIAL_REG_IER  (DRV_SERIAL_COM1_BASE + 1U) /* interrupt enable (DLAB high byte) */
#define DRV_SERIAL_REG_FCR  (DRV_SERIAL_COM1_BASE + 2U) /* FIFO control */
#define DRV_SERIAL_REG_LCR  (DRV_SERIAL_COM1_BASE + 3U) /* line control */
#define DRV_SERIAL_REG_MCR  (DRV_SERIAL_COM1_BASE + 4U) /* modem control */
#define DRV_SERIAL_REG_LSR  (DRV_SERIAL_COM1_BASE + 5U) /* line status */

#define DRV_SERIAL_LSR_THRE 0x20U /* transmitter holding register empty */

void drv_serial_init(void)
{
    core_io_out8(DRV_SERIAL_REG_IER, 0x00U);  /* no UART interrupts */
    core_io_out8(DRV_SERIAL_REG_LCR, 0x80U);  /* DLAB on */
    core_io_out8(DRV_SERIAL_REG_DATA, 0x01U); /* divisor 1 -> 115200 baud */
    core_io_out8(DRV_SERIAL_REG_IER, 0x00U);
    core_io_out8(DRV_SERIAL_REG_LCR, 0x03U);  /* 8 data bits, no parity, DLAB off */
    core_io_out8(DRV_SERIAL_REG_FCR, 0xC7U);  /* FIFO on, cleared, 14-byte trigger */
    core_io_out8(DRV_SERIAL_REG_MCR, 0x0BU);  /* DTR + RTS + OUT2 */
}

void drv_serial_putc(char c)
{
    while ((core_io_in8(DRV_SERIAL_REG_LSR) & DRV_SERIAL_LSR_THRE) == 0U) {
        /* wait until the transmitter holding register is empty */
    }
    core_io_out8(DRV_SERIAL_REG_DATA, (uint8_t)c);
}

void drv_serial_puts(const char *s)
{
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        drv_serial_putc(s[i]);
    }
}
