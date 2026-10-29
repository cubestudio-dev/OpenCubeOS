/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "rmdir".
 * Moved verbatim from the ush `rmdir` builtin: removes argv[1],
 * reporting "rmdir: cannot remove '<path>'" on failure. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- rmdir <path> ---------- */
    if (argc < 2) {
        puts_("usage: rmdir <path>\n");
        return 0;
    }
    if (sys_rmdir(argv[1]) < 0) {
        puts_("rmdir: cannot remove '");
        puts_(argv[1]);
        puts_("'\n");
    }
    return 0;
}
