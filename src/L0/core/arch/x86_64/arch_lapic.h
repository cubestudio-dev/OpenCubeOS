/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: kernel/arch/x86_64/arch_lapic.h
 * Purpose: Local APIC (xAPIC mode) + message-signaled interrupt
 *          dispatch. The LAPIC is the delivery target of every MSI /
 *          MSI-X write on x86; this module enables it, provides EOI,
 *          self-IPI (the same message-write path a device MSI uses)
 *          and the vector dispatch table MSI-capable devices are
 *          programmed with.
 *
 * Vector layout:
 *   0x20..0x2F  8259 PIC (unchanged, WP-02)
 *   0x30..0x6F  MSI / MSI-X dispatch vectors (arch_lapic_* API)
 *   0x80        syscall (unchanged)
 *   0xFF        spurious (ignored)
 */
#ifndef OC_ARCH_LAPIC_H
#define OC_ARCH_LAPIC_H

#include "types.h"
#include "arch_idt.h"
#include "arch_irq.h"

/* Initialize/detect the LAPIC. Safe to call twice (S3 resume path
 * calls it again after firmware reset). Returns 0 when the LAPIC is
 * enabled, -1 when the CPU has no APIC. */
int  arch_lapic_init(void);

/* 1 when the LAPIC was detected and enabled. */
int  arch_lapic_present(void);

/* LAPIC ID (physical, xAPIC 8-bit form). */
u32  arch_lapic_id(void);

/* End-of-interrupt for MSI vectors (and self-IPIs). */
void arch_lapic_eoi(void);

/* Send a self-IPI with the given vector - exercises the exact
 * message-write delivery path an MSI-capable device uses. */
int  arch_lapic_send_self_ipi(u8 vector);

/* Register/unregister a handler on an MSI dispatch vector
 * (0x30..0x6F). Up to 4 handlers per vector (shared semantics like
 * the PIC chains). Returns 0 on success. */
int  arch_lapic_register_vector(int vector, arch_irq_handler_fn handler, void *ctx);
int  arch_lapic_unregister_vector(int vector, arch_irq_handler_fn handler, void *ctx);

/* Fire count per vector (for the msitest command). */
u64  arch_lapic_vector_count(int vector);

/* Dispatch entry called by the IDT layer for vectors 0x30..0x6F.
 * Sends the LAPIC EOI first (same early-EOI policy as the PIC path). */
void arch_msi_dispatch(arch_irq_frame_t *f);

#endif /* OC_ARCH_LAPIC_H */
