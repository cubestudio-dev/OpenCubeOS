/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "top".
 * Moved verbatim from the ush `top` builtin: prints a real snapshot
 * (uptime + SYS_MEMINFO + process table); `-n N` refreshes up to 10
 * times, one second apart (uptime spin).
 * WP-runtime-hardening i18n: the human fragments (uptime unit, the
 * Mem sentence) render through tr_(); the table header and the
 * state words stay English BY CONTRACT - "top - up" and "TID" are
 * E2E anchors (tools/wp10_wp08fix1_test.py) and the zh_CN column
 * skeleton convention keeps the alignment (see apply_tool_i18n.py). */
#include "l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- top (WP-10-wp08fix1: snapshot + optional refresh) ----------
     * Rolling-screen TUI is impossible on a scrolling framebuffer console,
     * so top prints a REAL snapshot (scheduler stats + memory + process
     * table).  -n N refreshes N times, one second apart (uptime spin). */
    int rounds = 1;
    if (argc > 2 && streq_(argv[1], "-n")) rounds = (int)atoi_(argv[2]);
    if (rounds < 1) rounds = 1;
    if (rounds > 10) rounds = 10;
    for (int r = 0; r < rounds; r++) {
        if (r > 0) {
            /* spin ~1 second between refreshes */
            u64 start = (u64)sys_uptime();
            while ((u64)sys_uptime() - start < 1000) { }
        }
        u64 now = (u64)sys_uptime();
        /* "top - up " is an E2E anchor (wp10_wp08fix1_test greps it) -
         * the prefix stays a literal, the unit translates */
        puts_("top - up ");
        putu_(now);
        puts_(tr_(" ms\n"));
        unsigned long long mi[3];
        if (sys_meminfo(mi) == 0) {
            puts_(tr_("Mem: "));
            putu_(mi[1]);
            puts_(tr_(" used / "));
            putu_(mi[0]);
            puts_(tr_(" total / "));
            putu_(mi[2]);
            puts_(tr_(" free\n"));
        }
        static struct ush_ps_entry ptab[32];
        long n = sys_ps(ptab, (long)sizeof(ptab));
        if (n >= 0) {
            /* header + state words: machine contract / column skeleton */
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
        }
        if (r + 1 < rounds) puts_("\n");
    }
    return 0;
}
