/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_idt.h
 * Purpose: Minimal IDT for OCIRE - the 32 CPU exception vectors only.
 *          External interrupts stay masked (OCIRE is a polling kernel);
 *          a fault must print a diagnostic instead of silently
 *          triple-faulting into a reset loop.
 */

#ifndef OCIRE_IDT_H
#define OCIRE_IDT_H

#include "ocire_types.h"

void ocire_idt_init(void);
/* C entry the asm stubs call: (vector, error, rip). */
void ocire_isr_trap(u64 vector, u64 error, u64 rip);

#endif /* OCIRE_IDT_H */
