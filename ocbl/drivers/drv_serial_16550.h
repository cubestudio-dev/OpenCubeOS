/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL COM1 16550 UART driver (WP-02-ocbl).
 *
 * Polled-mode output on the first 16550 UART (COM1, I/O port base
 * 0x3F8): init to 115200 8N1 with FIFOs enabled, then byte-wise
 * transmit with the transmitter-holding-register-empty handshake.
 * This is the CORE debug channel shared by both boot firmware paths
 * (BIOS and UEFI) - the one output that is always available.
 *
 * Compiled by BOTH toolchains (gcc ELF64 and mingw PE32+); includes
 * nothing platform-specific. The I/O helpers come from
 * lib/core_io_port.h.
 *
 * Naming: [class]_[category]_[role] - drv (driver), serial (device
 * class), 16550 (the specific UART this driver targets).
 */

#ifndef DRV_SERIAL_16550_H
#define DRV_SERIAL_16550_H

#include <stdint.h>

/* Initialise COM1 (115200 baud, 8 data bits, no parity, FIFO on). */
void drv_serial_init(void);

/* Send one byte (polls until the transmitter holding register is
 * empty). '\n' is sent as-is. */
void drv_serial_putc(char c);

/* Send a NUL-terminated string, byte by byte. */
void drv_serial_puts(const char *s);

#endif /* DRV_SERIAL_16550_H */
