/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/diag kernel-side exports.
 *
 * The v2.2.0 exception self-test block lived static in core/main.c and
 * was shared between the boot self-test and the `exc` shell command
 * (one counter, both callers).  3.0.0-WP03 kept it static in kmain.c
 * (boot step 11);  WP-04 moves the block into services/diag so the
 * boot step and the ring-3 `exc` op (L1SVC cat "diag" op 10) again
 * share ONE counter - the exact v2.2.0 relationship.  kmain calls
 * arch_exc_tests() unchanged; only the file it lives in moved
 * (boot -> services is the legal dependency direction). */
#ifndef OC_DIAG_KERN_H
#define OC_DIAG_KERN_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* services/diag/diag_exc_test.c - v2.2.0 core/main.c block, verbatim */
extern u64 g_exc_test_pass_count;
void arch_exc_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_DIAG_KERN_H */
