/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/exceptions.h
 * Purpose: CPU exception dispatch + L1 exception-handler registration.
 */
#ifndef OC_EXCEPTIONS_H
#define OC_EXCEPTIONS_H

#include "idt.h"

/* Initialize the default exception handler table. Called from oc_idt_init. */
void oc_exc_init(void);

/* Dispatch a CPU exception (vector 0..31). Called from oc_isr_dispatch. */
void oc_exc_dispatch(oc_irq_frame_t *f);

/* ---- L1 extension: exception handler registration ----
 *
 * L1 can register a handler for any exception vector (0..31). The handler
 * receives the full interrupt frame (including error code, faulting RIP,
 * etc.). If the handler returns 0, L0 prints a diagnostic and halts the
 * kernel. If it returns non-zero, L0 assumes L1 handled the fault (e.g.
 * by fixing up the RIP for a JIT) and resumes.
 *
 * signature:
 *   int (*oc_exc_handler_fn)(oc_irq_frame_t *f);
 *   return 0 = let L0 halt; non-zero = L1 handled, resume.
 */
typedef int (*oc_exc_handler_fn)(oc_irq_frame_t *f);

/* Register a handler for `vector` (0..31). Returns 0 on success.
 * Multiple registrations on the same vector form a chain; the most recently
 * registered handler runs first. */
int oc_exc_register_handler(int vector, oc_exc_handler_fn handler);

/* Unregister a handler. Returns 0 on success. */
int oc_exc_unregister_handler(int vector, oc_exc_handler_fn handler);

#endif /* OC_EXCEPTIONS_H */
