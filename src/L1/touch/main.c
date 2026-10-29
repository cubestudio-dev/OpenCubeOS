/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "touch".
 * Moved verbatim from the ush `touch` builtin: open(argv[1], 6)
 * (WRONLY|CREAT) + close - creates the file or leaves it untouched. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- touch <file> ---------- */
    if (argc < 2) {
        puts_("usage: touch <file>\n");
        return 0;
    }
    long fd = sys_open(argv[1], 6);
    if (fd >= 0) sys_close(fd);
    return 0;
}
