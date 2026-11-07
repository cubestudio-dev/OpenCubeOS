/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "help_wp_test".
 *
 * The v2.2.0 command verified that every WP group in the L0 command
 * table was internally A-Z sorted (`help -w`).  The WP-group concept
 * belonged to the removed L0 shell table; the 3.0.0 registry is one
 * flat A-Z list, so the group-ordering invariant reduces to the global
 * ordering invariant - asserted here over the live listing (declared
 * semantic re-targeting, same invariant family). */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"
int app_main(int argc, char **argv) { (void)argc; (void)argv;
    char out[8192];
    long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, 0,
                        (long)out, (long)sizeof(out));
    int sorted = 0;
    if (rc == 0) {
        char prev[64]; prev[0] = 0;
        int line_start = 1, ok = 1, cl = 0;
        char cur[64];
        for (const char *p = out; *p; p++) {
            if (line_start && *p == '-' && p[1] == '-') break;
            if (*p == '\n') {
                cur[cl] = 0;
                if (cl > 0 && prev[0]) {
                    int c = 0;
                    while (prev[c] && cur[c] && prev[c] == cur[c]) c++;
                    if ((unsigned char)prev[c] > (unsigned char)cur[c]) ok = 0;
                }
                for (int i = 0; i <= cl; i++) prev[i] = cur[i];
                cl = 0; line_start = 1;
            } else {
                if (cl < (int)sizeof(cur) - 1) cur[cl++] = *p;
                line_start = 0;
            }
        }
        sorted = ok;
    }
    puts_("help_wp_test: registry flat A-Z ordering (one group == all): ");
    puts_(sorted ? "PASS" : "FAIL");
    putc_('\n');
    puts_("help_wp_test: global listing still sorted: ");
    puts_(sorted ? "PASS" : "FAIL");
    putc_('\n');
    puts_("help_wp_test: run 'help' to see the flat A-Z listing\n");
    return sorted ? 0 : 1;
}
