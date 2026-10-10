/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-AUDIT_P2-fix3 G8
 * Purpose: clock/power/timer/cr3test regression suite (BUG-0287..0289,
 *          0291) for l1test step 10.  BUG-0290 (non-volatile spin flag)
 *          is covered by disassembly + run-ush behaviour evidence; there
 *          is no safe in-kernel probe that can observe the compiler's
 *          load hoisting from inside the running image.
 *
 * The suite is written so the SAME source compiles against the pre-fix
 * baseline (probe build) and the fixed tree, same approach as g7test:
 *   - OC_G8_RTC_DECODE      (core_rtc.h, G8 batch): the raw-snapshot
 *     decoder is exposed for the A-group injection assertions.
 *   - OC_G8_HAS_CR3TEST     (below): main.c exposes shell_cmd_cr3test
 *     (non-static since the G8 batch) so the E-group can drive it with
 *     a drained PMM.
 */
#ifndef OC_G8TEST_H
#define OC_G8TEST_H

#define OC_G8_HAS_CR3TEST 1

int g8test_run(void);

#endif /* OC_G8TEST_H */
