/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "date".
 * Moved verbatim from the ush `date` builtin: the kernel has no RTC
 * driver, so it reports the kernel uptime (PIT tick counter in ms
 * since boot) via SYS_UPTIME instead of a fabricated date string. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* ---------- date ----------
     * P2-04 FIX: the kernel has no RTC driver, so we cannot read a real
     * wall-clock date. Instead of printing a hardcoded fake date string,
     * we report the kernel uptime (PIT tick counter in ms since boot) via
     * SYS_UPTIME. This is a real value from the kernel, not fabricated. */
    puts_("Open Cube OS uptime: ");
    putu_((u64)sys_uptime());
    puts_(" ms since boot (no RTC driver)\n");
    return 0;
}
