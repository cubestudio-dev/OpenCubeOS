/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "mv".
 * Moved verbatim from the ush `mv` builtin: copy argv[1] to argv[2]
 * (O_TRUNC target, BUG-0089 fix) then unlink the source. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- mv <src> <dst> ---------- */
    if (argc < 3) {
        puts_("usage: mv <src> <dst>\n");
        return 0;
    }
    long sfd = sys_open(argv[1], 1);
    if (sfd < 0) {
        puts_("mv: cannot open '");
        puts_(argv[1]);
        puts_("'\n");
        return 0;
    }
    /* BUG-0089 FIX (A16-2): O_TRUNC on the move target - an existing
     * longer destination kept its stale tail after a shorter move. */
    long dfd = sys_open(argv[2], 22);
    if (dfd < 0) {
        puts_("mv: cannot open '");
        puts_(argv[2]);
        puts_("'\n");
        sys_close(sfd);
        return 0;
    }
    static char mvbuf[4096];
    long n;
    while ((n = sys_read(sfd, mvbuf, 4096)) > 0) {
        sys_write(dfd, mvbuf, n);
    }
    sys_close(sfd);
    sys_close(dfd);
    sys_unlink(argv[1]);
    return 0;
}
