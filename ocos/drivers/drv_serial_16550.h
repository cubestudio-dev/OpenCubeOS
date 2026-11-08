/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - COM1 16550 UART driver (WP-01-tree).
 *
 * Polled-mode output on the first 16550 UART (COM1, I/O port base
 * 0x3F8): init to 115200 8N1 with FIFOs enabled, then byte-wise
 * transmit with the transmitter-holding-register-empty handshake.
 * No interrupts, no receive path - the boot banner only needs to go
 * out. Those arrive in later work packages.
 *
 * Naming: [class]_[category]_[role] - drv (driver), serial (device
 * class), 16550 (the specific UART this driver targets).
 */

#ifndef DRV_SERIAL_16550_H
#define DRV_SERIAL_16550_H

/* Initialise COM1 (115200 baud, 8 data bits, no parity, FIFO on). */
void drv_serial_init(void);

/* Send one byte (polls until the transmitter holding register is
 * empty). '\n' is sent as-is; the receiving side decides about CRLF. */
void drv_serial_putc(char c);

/* Send a NUL-terminated string, byte by byte. */
void drv_serial_puts(const char *s);

#endif /* DRV_SERIAL_16550_H */
