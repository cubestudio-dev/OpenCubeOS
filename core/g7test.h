/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-AUDIT_P2-fix3 G7
 * File: kernel/core/g7test.h
 * Purpose: Regression suite for the G7 batch (BUG-0281..0286):
 *          mmap window/prot/addr (BUG-0281), reaper mutual exclusion
 *          (BUG-0282), idle stack allocation check (BUG-0283), the
 *          "idle is never queued" invariant (BUG-0284), IRQ-safe
 *          spinlocks (BUG-0285) and mutex FIFO waiter management
 *          (BUG-0286). The mmap side lives in the g7mmap user program
 *          (it needs a real ring-3 address space); this suite covers
 *          the scheduler/synchronization side from kernel context. */
#ifndef OC_G7TEST_H
#define OC_G7TEST_H

/* Run the suite. Returns the number of FAILED assertions (0 = all pass).
 * Prints one PASS/FAIL line per assertion plus a G7TEST summary. */
int g7test_run(void);

#endif /* OC_G7TEST_H */
