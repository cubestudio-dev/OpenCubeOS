/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ps".
 * Moved verbatim from the ush `ps` builtin: dumps the real kernel
 * process table via SYS_PS (TID/state/priority/CPU ticks/name). */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* ---------- ps (WP-10-wp08fix1: real kernel process table) ---------- */
    static struct ush_ps_entry ptab[32];
    long n = sys_ps(ptab, (long)sizeof(ptab));
    if (n < 0) {
        puts_("ps: SYS_PS unavailable (kernel too old)\n");
        return 0;
    }
    puts_("TID  STATE  PRI  CPU(ticks)  NAME\n");
    for (long i = 0; i < n; i++) {
        putu_((u64)ptab[i].tid);
        puts_("    ");
        switch (ptab[i].state) {
            case 0: puts_("READY "); break;
            case 1: puts_("RUN   "); break;
            case 2: puts_("BLOCK "); break;
            case 3: puts_("EXIT  "); break;
            default: puts_("?     "); break;
        }
        putu_((u64)ptab[i].priority);
        puts_("    ");
        putu_(ptab[i].cpu_time_ticks);
        puts_("          ");
        puts_(ptab[i].name);
        putc_('\n');
    }
    return 0;
}
