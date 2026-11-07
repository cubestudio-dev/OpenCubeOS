/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "health".
 *
 * The v2.2.0 `health` command family (health / health watch /
 * health export) as ONE app: the subcommand word in argv[1] selects the
 * service op (the v2.2.0 shell dispatched the same words inside one
 * command).  Default (no subcommand) = the fresh-sample report. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

static long call(long op, const char *arg) {
    char out[4096];
    long rc = sys_l1svc(L1SVC_CAT_HEALTH, op,
                        arg ? (long)arg : 0, (long)out, (long)sizeof(out));
    if (rc != 0) {
        puts_("health: service call failed (rc=-1)\n");
        return 1;
    }
    puts_(out);
    if (out[0] && out[strlen_(out) - 1] != '\n') putc_('\n');
    return 0;
}

int app_main(int argc, char **argv) {
    if (argc < 2) return call(L1SVC_HEALTH_SNAPSHOT, 0);
    if (streq_(argv[1], "watch")) return call(L1SVC_HEALTH_WATCH, 0);
    if (streq_(argv[1], "export")) {
        /* join any path argument */
        char path[256];
        int len = 0;
        path[0] = 0;
        for (int j = 2; j < argc && len < (int)sizeof(path) - 1; j++) {
            if (j > 2 && len < (int)sizeof(path) - 2) path[len++] = ' ';
            for (const char *p = argv[j]; *p && len < (int)sizeof(path) - 1; p++)
                path[len++] = *p;
            path[len] = 0;
        }
        return call(L1SVC_HEALTH_EXPORT, path[0] ? path : 0);
    }
    puts_("usage: health [watch|export [path]]\n");
    return 0;
}
