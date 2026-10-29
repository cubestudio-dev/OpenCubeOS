/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ln".
 * Moved verbatim from the ush `ln` builtin (WP-10-wp08fix1): `ln -s
 * <abs-target> <linkpath>` makes a symlink, `ln <old> <new>` a hard
 * link, with the errno-ish decimal on failure. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- ln [-s] <target/old> <linkpath> ---------- */
    if (argc >= 4 && streq_(argv[1], "-s")) {
        if (sys_symlink(argv[2], argv[3]) < 0) {
            puts_("ln: cannot create symlink (target must be an absolute path)\n");
        }
        return 0;
    }
    if (argc < 3) {
        puts_("usage: ln <old> <new> | ln -s <abs-target> <linkpath>\n");
        return 0;
    }
    long lrc = sys_link(argv[1], argv[2]);
    if (lrc < 0) {
        puts_("ln: hard link failed (rc=");
        /* small decimal print of the errno-ish value */
        long v = -lrc;
        char nb[12];
        int ni = 0;
        if (v == 0) nb[ni++] = '0';
        while (v > 0 && ni < 11) { nb[ni++] = (char)('0' + (v % 10)); v /= 10; }
        nb[ni] = 0;
        /* reverse */
        for (int a = 0, b = ni - 1; a < b; a++, b--) {
            char tc = nb[a]; nb[a] = nb[b]; nb[b] = tc;
        }
        puts_(nb);
        puts_(")\n");
    }
    return 0;
}
