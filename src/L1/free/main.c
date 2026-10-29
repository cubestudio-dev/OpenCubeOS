/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "free".
 * Moved verbatim from the ush `free` builtin: real PMM numbers via
 * SYS_MEMINFO (WP-09-FIX BUG-014) instead of hardcoded fake data. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* ---------- free ----------
     * WP-09-FIX BUG-014: query real PMM numbers via SYS_MEMINFO
     * instead of printing hardcoded fake data (the old line claimed
     * 128 MiB total on a 512 MiB VM). Falls back to an honest
     * message if the syscall is unavailable. */
    unsigned long long mi[3];
    if (sys_meminfo(mi) == 0) {
        puts_("              total        used        free\n");
        puts_("Mem:     ");
        putu_(mi[0]); puts_(" ");
        putu_(mi[1]); puts_(" ");
        putu_(mi[2]); puts_("\n");
        puts_("Swap:            0           0            0\n");
    } else {
        puts_("free: SYS_MEMINFO unavailable (kernel too old)\n");
    }
    return 0;
}
