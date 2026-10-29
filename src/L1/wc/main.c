/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "wc".
 * Moved verbatim from the ush `wc` builtin: counts lines/words/chars of
 * argv[1] (RDONLY - a missing path is an error, creates nothing) or
 * stdin, printing "<lines> <words> <chars> [file]". */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- wc [file] ---------- */
    static char wbuf[8192];
    long total = 0, lines = 0, words = 0;
    int in_word = 0;
    long fd = 0;
    int opened = 0;
    if (argc > 1) {
        /* A16-6 FIX: wc only READS the file, but flag 6 =
         * WRONLY(2)|CREAT(4) silently CREATED an empty file when the
         * path did not exist and reported a bogus `0 0 0`. RDONLY(1)
         * is the correct mode: a missing path now reports an error
         * and creates nothing (same flag cat/tail/sed use). */
        fd = sys_open(argv[1], 1);
        if (fd < 0) {
            puts_("wc: cannot open ");
            puts_(argv[1]);
            putc_('\n');
            return 0;
        }
        opened = 1;
    }
    long n;
    while ((n = sys_read(fd, wbuf, 8192)) > 0) {
        total += n;
        for (int i = 0; i < (int)n; i++) {
            char c = wbuf[i];
            if (c == '\n') lines++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    if (opened) sys_close(fd);
    putu_((u64)lines);
    putc_(' ');
    putu_((u64)words);
    putc_(' ');
    putu_((u64)total);
    if (argc > 1) { putc_(' '); puts_(argv[1]); }
    putc_('\n');
    return 0;
}
