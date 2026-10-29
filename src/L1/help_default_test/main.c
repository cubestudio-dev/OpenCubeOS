/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "help_default_test".
 *
 * The v2.2.0 command verified the A-Z ordering invariant of the L0
 * command table behind `help`.  The table moved: in 3.0.0 the command
 * surface is the OC_USERPROGS registry, so this test asserts the same
 * invariant over the live registry listing (L1SVC cat "l1" op 1) -
 * every registered name must be strictly ascending. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

static int verify_sorted(void) {
    char out[8192];
    long rc = sys_l1svc(L1SVC_CAT_L1, L1SVC_L1_APPS, 0,
                        (long)out, (long)sizeof(out));
    if (rc != 0) return 0;
    char prev[64];
    prev[0] = 0;
    int line_start = 1;
    int ok = 1;
    char cur[64]; int cl = 0;
    for (const char *p = out; *p; p++) {
        if (line_start && *p == '-' && p[1] == '-') break;  /* footer */
        if (*p == '\n') {
            cur[cl] = 0;
            if (cl > 0 && prev[0]) {
                /* strict A-Z ascending */
                int c = 0;
                while (prev[c] && cur[c] && prev[c] == cur[c]) c++;
                if ((unsigned char)prev[c] > (unsigned char)cur[c]) ok = 0;
            }
            for (int i = 0; i <= cl; i++) prev[i] = cur[i];
            cl = 0;
            line_start = 1;
        } else {
            if (cl < (int)sizeof(cur) - 1) cur[cl++] = *p;
            line_start = 0;
        }
    }
    return ok;
}

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    int sorted = verify_sorted();
    puts_("help_default_test: A-Z ordering of the full command registry: ");
    puts_(sorted ? "PASS" : "FAIL");
    putc_('\n');
    puts_("help_default_test: run 'help' to see the full A-Z listing\n");
    return sorted ? 0 : 1;
}
