/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: drivers/display/screen_serial_in.h
 * Purpose: COM1 serial input (UART RX IRQ) feeding the keyboard input queue.
 *
 * COM1 RX fires IRQ4 (vector 36). We read the byte, translate CR?Enter,
 * BS?Backspace, and push the resulting keycode into the keyboard queue so
 * both input sources are unified.
 */
#ifndef OC_SERIAL_IN_H
#define OC_SERIAL_IN_H

#include "types.h"
#include "arch_idt.h"   /* for arch_irq_frame_t */

void screen_serial_in_init(void);

/* IRQ4 handler (called from arch_irq_dispatch). */
void screen_serial_in_irq_handler(void *ctx, arch_irq_frame_t *f);

/* K-09 (WP-severe-fix-and-closure): the 10 ms tick-bailout RX poll.
 * Belt-and-braces backstop for the 16550-level-INTR-behind-edge-8259
 * loss window (a burst landing mid-service raises no new edge; after
 * the EOI the remainder sat uncollected - a pasted long command line
 * could lose every byte). Registered from screen_serial_in_init();
 * idempotent with the IRQ handler (both feed the same per-byte
 * decoder). */
void screen_serial_in_poll(void *ctx);

#endif /* OC_SERIAL_IN_H */
