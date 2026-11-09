/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_console.h
 * Purpose: OCIRE console - every character goes to BOTH the 16550
 *          serial port (COM1, 115200 8N1) and the video backend.
 *          WP-02: the video backend is the multiboot2 linear
 *          framebuffer when GRUB provided an RGB one (the OCIRE
 *          header requests 800x600x32), and the 80x25 VGA text buffer
 *          at 0xB8000 otherwise.  The serial half is what headless
 *          QEMU runs and pexpect tests drive; the video half is what
 *          a real monitor shows.
 */

#ifndef OCIRE_CONSOLE_H
#define OCIRE_CONSOLE_H

#include "ocire_types.h"

void ocire_console_init(void);
void ocire_putc(char c);
void ocire_puts(const char *s);
/* Print "label0x" + value in hex, exactly width nibbles (zero-padded). */
void ocire_puthex(const char *label, u64 value, int nibbles);
/* Print "label" + value in decimal. */
void ocire_putdec(const char *label, u64 value);

/* 1 = the LFB backend is active (fb tag present), 0 = VGA text. */
int  ocire_console_is_lfb(void);

/* Serial RX half (used by the input poller in ocire_input.c). */
int  ocire_serial_rx_ready(void);
char ocire_serial_rx(void);

#endif /* OCIRE_CONSOLE_H */
