/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/irq.h
 * Purpose: IRQ dispatch table + L1 IRQ-handler registration.
 *
 * IRQs 0..15 (PIC-mapped) arrive as vectors 32..47. Each IRQ can have
 * up to 4 registered handlers (shared IRQ support).
 */
#ifndef OC_IRQ_H
#define OC_IRQ_H

#include "idt.h"

void oc_irq_init(void);
void oc_irq_dispatch(oc_irq_frame_t *f);

/* ---- L1 extension: IRQ handler registration ----
 *
 * `irq` is 0..15 (the PIC IRQ number, NOT the vector).
 * The handler is called with the (unused) frame pointer and a `ctx`
 * pointer the L1 driver supplied at registration time.
 *
 * Handlers run in IRQ context (interrupts may be off). They must be fast
 * and must NOT block. If a handler needs to do slow work it should defer
 * to a bottom-half (WP-04+).
 *
 * Returns 0 on success. Handlers can be shared: multiple handlers on the
 * same IRQ all run on each IRQ fire.
 */
typedef void (*oc_irq_handler_fn)(void *ctx, oc_irq_frame_t *f);

int oc_irq_register_handler(int irq, oc_irq_handler_fn handler, void *ctx);
int oc_irq_unregister_handler(int irq, oc_irq_handler_fn handler, void *ctx);

#endif /* OC_IRQ_H */
