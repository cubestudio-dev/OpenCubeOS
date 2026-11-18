/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - VERIFICATION VIRTUAL DRIVER (tests-only).
 *
 *   *** NOT A KERNEL DRIVER.  NOT HARDWARE. ***
 *
 * vtest-cyc-a / vtest-cyc-b form a DECLARED dependency cycle.  The
 * order computation must detect the cycle, isolate BOTH members
 * with a boot-log report, and never spin: they carry no callbacks
 * at all (they must never run), which makes any accidental init
 * instantly visible. */
#include "core_drv_meta.h"

static const char *const g_dep_a[] = { "vtest-cyc-b", 0 };
static const char *const g_dep_b[] = { "vtest-cyc-a", 0 };

OC_DRIVER_INFO_DECL(
    .name    = "vtest-cyc-a",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .depends = g_dep_a,
    .desc    = "verification cycle canary A (tests/drv_framework, not hw)");

OC_DRIVER_INFO_DECL(
    .name    = "vtest-cyc-b",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .depends = g_dep_b,
    .desc    = "verification cycle canary B (tests/drv_framework, not hw)");
