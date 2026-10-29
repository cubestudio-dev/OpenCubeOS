/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "uniq".
 * Moved verbatim from the ush `uniq` builtin: reads argv[1] (P2-03 fix)
 * or stdin, printing consecutive duplicate lines only once. */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- uniq (file or stdin) ---------- */
    static char ubuf[4096];
    static char uprev[1024];
    static char uline[1024];
    uprev[0] = 0;
    int has_prev = 0;
    int lpos = 0;
    long n;
    /* P2-03 FIX: if a filename argument is provided, read from the
     * file instead of stdin. */
    long src_fd = 0;
    int opened = 0;
    if (argc >= 2) {
        long fd = sys_open(argv[1], 1);  /* VFS_O_RDONLY (kernel flag 1 = read) */
        if (fd < 0) {
            puts_("uniq: cannot open ");
            puts_(argv[1]);
            putc_('\n');
            return 0;
        }
        src_fd = fd;
        opened = 1;
    }
    while ((n = sys_read(src_fd, ubuf, 4096)) > 0) {
        for (int i = 0; i < (int)n; i++) {
            char c = ubuf[i];
            if (c == '\n') {
                uline[lpos] = 0;
                if (!has_prev || !streq_(uprev, uline)) {
                    puts_(uline);
                    putc_('\n');
                    strncpy_(uprev, uline, 1024);
                    has_prev = 1;
                }
                lpos = 0;
            } else {
                if (lpos < 1023) uline[lpos++] = c;
            }
        }
    }
    if (opened) sys_close(src_fd);
    if (lpos > 0) {
        uline[lpos] = 0;
        if (!has_prev || !streq_(uprev, uline)) {
            puts_(uline);
            putc_('\n');
        }
    }
    return 0;
}
