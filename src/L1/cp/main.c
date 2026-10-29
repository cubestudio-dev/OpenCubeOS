/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "cp".
 * Moved verbatim from the ush `cp` builtin: copies argv[1] to argv[2]
 * with O_TRUNC on the target (BUG-0089 fix). */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- cp <src> <dst> ---------- */
    if (argc < 3) {
        puts_("usage: cp <src> <dst>\n");
        return 0;
    }
    long sfd = sys_open(argv[1], 1);
    if (sfd < 0) {
        puts_("cp: cannot open '");
        puts_(argv[1]);
        puts_("'\n");
        return 0;
    }
    /* BUG-0089 FIX (A16-2): O_TRUNC on the copy target - an existing
     * longer destination kept its stale tail after a shorter copy. */
    long dfd = sys_open(argv[2], 22);
    if (dfd < 0) {
        puts_("cp: cannot open '");
        puts_(argv[2]);
        puts_("'\n");
        sys_close(sfd);
        return 0;
    }
    static char cpbuf[4096];
    long n;
    while ((n = sys_read(sfd, cpbuf, 4096)) > 0) {
        sys_write(dfd, cpbuf, n);
    }
    sys_close(sfd);
    sys_close(dfd);
    return 0;
}
