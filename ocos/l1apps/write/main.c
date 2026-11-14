/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "write".
 * Moved verbatim from the ush `write` builtin (the v1 oc> write command
 * as a file syscall tool): writes argv[2..] joined by spaces + newline
 * into argv[1] (O_CREAT, no truncate - as the shell builtin did). */
#include "l1_lib.h"

int app_main(int argc, char **argv) {
    /* v2.0.0: the v1 oc> `write <path> <text>` command, as a ush
     * builtin through the classic file syscalls. */
    if (argc < 3) { puts_("usage: write <path> <text>\n"); return 0; }
    /* BUG #35 FIX (P1, WP-fix-critical-security): the app opened the
     * target with flag 1 = O_RDONLY (the comment claimed WRONLY|CREAT
     * but the value was the RDONLY constant) - every write then failed
     * and the failures were IGNORED, so `write` has been a silent
     * no-op since release.  Open WRONLY|CREAT (6, v1 semantics: no
     * truncate) and CHECK every write's return. */
    long fd = sys_open(argv[1], 6);
    if (fd < 0) { puts_("write: cannot open "); puts_(argv[1]); putc_('\n'); return 0; }
    long fail = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && sys_write(fd, " ", 1) != 1) fail = 1;
        long wl = (long)strlen_(argv[i]);
        if (sys_write(fd, argv[i], wl) != wl) fail = 1;
    }
    if (sys_write(fd, "\n", 1) != 1) fail = 1;
    if (fail) {
        puts_("write: write failed on ");
        puts_(argv[1]);
        putc_('\n');
    }
    sys_close(fd);
    return fail ? 1 : 0;
}
