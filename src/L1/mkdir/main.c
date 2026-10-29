/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "mkdir".
 * Moved verbatim from the ush `mkdir` builtin: creates argv[1],
 * reporting "mkdir: cannot create '<path>'" on failure. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- mkdir <path> ---------- */
    if (argc < 2) {
        puts_("usage: mkdir <path>\n");
        return 0;
    }
    if (sys_mkdir(argv[1]) < 0) {
        puts_("mkdir: cannot create '");
        puts_(argv[1]);
        puts_("'\n");
    }
    return 0;
}
