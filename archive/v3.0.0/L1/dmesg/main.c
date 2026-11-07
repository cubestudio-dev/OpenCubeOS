/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "dmesg".
 * The v2.2.0 `dmesg [N]` command: kernel log ring replay, default 30
 * lines.  Body lives in services/klog (L1SVC cat "klog" op 1). */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    char arg[64];
    int len = 0;
    arg[0] = 0;
    for (int j = 1; j < argc && len < (int)sizeof(arg) - 1; j++) {
        for (const char *p = argv[j]; *p && len < (int)sizeof(arg) - 1; p++)
            arg[len++] = *p;
        arg[len] = 0;
    }
    char out[4096];
    long rc = sys_l1svc(L1SVC_CAT_KLOG, L1SVC_KLOG_DMESG,
                        arg[0] ? (long)arg : 0, (long)out, (long)sizeof(out));
    if (rc != 0) {
        puts_("dmesg: service call failed (rc=-1)\n");
        return 1;
    }
    puts_(out);
    if (out[0] && out[strlen_(out) - 1] != '\n') putc_('\n');
    return 0;
}
