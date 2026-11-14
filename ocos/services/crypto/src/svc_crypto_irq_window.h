/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-AUDIT_P2-fix3 G9 (BUG-0294 / A4-07)
 * File: kernel/crypto/crypto_irq_window.h
 * Purpose: single-core interrupt-critical window used by the crypto lazy
 *   initializers (AES inverse S-box, NIST curve tables).
 *
 * Honest semantics (BUG-0294): Open Cube OS is a single-CPU kernel, so the
 * only reentrancy source for these init paths is an interrupt handler on
 * the same CPU. The window below is a real save/cli/restore pair: it
 * captures RFLAGS, disables interrupts for the (microseconds-long)
 * check+fill+publish, then restores the EXACT saved RFLAGS through
 * pushq+popfq - the same irqsave shape as core_sched.c / mem_heap.c /
 * mem_pmm.c - so callers that already ran with IF=0 keep IF=0 and a plain
 * caller's other flags (DF etc.) are untouched too. This is NOT SMP
 * protection and NOT a lock: there is no spin, no wait, no guarantee
 * beyond "no interrupt interleaves inside the window".
 *
 * Host test builds (tests/host_*.c compile these .c files at CPL3 without
 * this Makefile's CFLAGS) expand to nothing: a userspace cli would fault,
 * and the single-threaded host harness has no interrupt source to close
 * the window against. The expansion is a build-environment property, not
 * a fake lock - the kernel build (the product) always gets the real asm.
 */
#ifndef OC_CRYPTO_IRQ_WINDOW_H
#define OC_CRYPTO_IRQ_WINDOW_H

#include "types.h"

#ifdef OC_KERNEL_BUILD

/* save RFLAGS, clear IF */
#define OC_IRQ_WINDOW_ENTER(flags_var) \
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags_var) :: "memory")
/* restore the EXACT saved RFLAGS: push the saved value back, then popfq.
 * A bare popfq here would pop whatever stack slot the compiler happens to
 * have at that point (garbage flags AND an 8-byte RSP desync the compiler
 * does not know about), so the saved value must be an input operand. */
#define OC_IRQ_WINDOW_LEAVE(flags_var) \
    __asm__ volatile("pushq %0; popfq" :: "r"(flags_var) : "memory")

#else

/* host test build: no privileged instructions, no interrupts */
#define OC_IRQ_WINDOW_ENTER(flags_var) ((void)(flags_var))
#define OC_IRQ_WINDOW_LEAVE(flags_var) ((void)(flags_var))

#endif

#endif /* OC_CRYPTO_IRQ_WINDOW_H */
