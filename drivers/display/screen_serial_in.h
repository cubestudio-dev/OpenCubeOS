/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/screen_serial_in.h
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

#endif /* OC_SERIAL_IN_H */
