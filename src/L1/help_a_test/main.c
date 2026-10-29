/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "help_a_test".
 * The v2.2.0 command verified `help -a` == the default A-Z view.  In
 * 3.0.0 there is one registry view served to `help` (and the former
 * `-a` flag), so this asserts the same ordering invariant as
 * help_default_test over the live listing. */
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
    puts_("help_a_test: 'help -a' == default A-Z view, ordering verified: ");
    puts_(sorted ? "PASS" : "FAIL");
    putc_('\n');
    return sorted ? 0 : 1;
}
