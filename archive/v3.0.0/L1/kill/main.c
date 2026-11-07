/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "kill".
 * Moved verbatim from the ush `kill` builtin: sends SYS_KILL to the
 * numeric pid in argv[1]. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- kill <pid> ---------- */
    if (argc < 2) {
        puts_("usage: kill <pid>\n");
        return 0;
    }
    long pid = (long)atoi_(argv[1]);
    if (sys_kill(pid) < 0) {
        puts_("kill: no such process\n");
    }
    return 0;
}
