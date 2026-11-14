/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/arch/x86_64/arch_exceptions.h
 * Purpose: CPU exception dispatch + L1 exception-handler registration.
 */
#ifndef OC_EXCEPTIONS_H
#define OC_EXCEPTIONS_H

#include "arch_idt.h"

/* Initialize the default exception handler table. Called from arch_idt_init. */
void arch_exc_init(void);

/* Dispatch a CPU exception (vector 0..31). Called from arch_isr_dispatch. */
void arch_exc_dispatch(arch_irq_frame_t *f);

/* ---- L1 extension: exception handler registration ----
 *
 * L1 can register a handler for any exception vector (0..31). The handler
 * receives the full interrupt frame (including error code, faulting RIP,
 * etc.). If the handler returns 0, L0 prints a diagnostic and halts the
 * kernel. If it returns non-zero, L0 assumes L1 handled the fault (e.g.
 * by fixing up the RIP for a JIT) and resumes.
 *
 * signature:
 *   int (*arch_exc_handler_fn)(arch_irq_frame_t *f);
 *   return 0 = let L0 halt; non-zero = L1 handled, resume.
 */
typedef int (*arch_exc_handler_fn)(arch_irq_frame_t *f);

/* Register a handler for `vector` (0..31). Returns 0 on success.
 * Multiple registrations on the same vector form a chain; the most recently
 * registered handler runs first. */
int arch_exc_register_handler(int vector, arch_exc_handler_fn handler);

/* Unregister a handler. Returns 0 on success. */
int arch_exc_unregister_handler(int vector, arch_exc_handler_fn handler);

/* 3.0.0-WP03 G5: the panic entry points are core-owned; the crash
 * service (services/crash) installs its implementation here. */
void kernel_panic(const char *reason);
void kernel_panic_frame(const char *reason, const arch_irq_frame_t *f);
void arch_set_panic_handlers(void (*panic)(const char *),
                             void (*panic_frame)(const char *,
                                                 const arch_irq_frame_t *));

void arch_set_crashlog_record(void (*record)(u32 vector, u64 error_code,
                                             u64 rip, u64 rsp, u64 cr2,
                                             const char *pname, i32 tid,
                                             i32 pid, u32 ring));

#endif /* OC_EXCEPTIONS_H */
