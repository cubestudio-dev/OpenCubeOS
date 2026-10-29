/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "chmod".
 * Moved verbatim from the ush `chmod` builtin (WP-10-wp08fix1): parses
 * the octal mode in argv[1] and applies it to argv[2] via SYS_CHMOD. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- chmod <octal-mode> <path> ---------- */
    if (argc < 3) {
        puts_("usage: chmod <mode> <path>   (mode in octal, e.g. 644)\n");
        return 0;
    }
    /* parse octal */
    u32 mode = 0;
    const char *m = argv[1];
    while (*m >= '0' && *m <= '7') { mode = mode * 8 + (u32)(*m - '0'); m++; }
    if (sys_chmod(argv[2], (long)mode) < 0) {
        puts_("chmod: cannot change mode\n");
    }
    return 0;
}
