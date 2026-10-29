/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "rm".
 * Moved verbatim from the ush `rm` builtin: unlinks argv[1], reporting
 * "rm: cannot remove '<path>'" on failure. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- rm <file> ---------- */
    if (argc < 2) {
        puts_("usage: rm <file>\n");
        return 0;
    }
    if (sys_unlink(argv[1]) < 0) {
        puts_("rm: cannot remove '");
        puts_(argv[1]);
        puts_("'\n");
    }
    return 0;
}
