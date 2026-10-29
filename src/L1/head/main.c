/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "head".
 * Moved verbatim from the ush `head` builtin: prints the first 10
 * lines (or `-N`/`N` when given) of stdin or the file argument. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- head [N] [file] ---------- */
    int nlines = 10;
    int argi = 1;
    if (argc > 1) {
        char *a = argv[1];
        if (a[0] == '-' || (a[0] >= '0' && a[0] <= '9')) {
            nlines = (int)atoi_(a[0] == '-' ? a + 1 : a);
            argi = 2;
        }
    }
    if (nlines < 1) nlines = 10;
    long fd = 0;
    int opened = 0;
    if (argi < argc) {
        fd = sys_open(argv[argi], 1);
        if (fd < 0) {
            puts_("head: cannot open ");
            puts_(argv[argi]);
            putc_('\n');
            return 0;
        }
        opened = 1;
    }
    static char hbuf[4096];
    static char hline[1024];
    int lpos = 0;
    int printed = 0;
    long n;
    while (printed < nlines && (n = sys_read(fd, hbuf, 4096)) > 0) {
        for (int i = 0; i < (int)n && printed < nlines; i++) {
            char c = hbuf[i];
            if (c == '\n') {
                hline[lpos] = 0;
                puts_(hline);
                putc_('\n');
                printed++;
                lpos = 0;
            } else {
                if (lpos < 1023) hline[lpos++] = c;
            }
        }
    }
    if (lpos > 0 && printed < nlines) {
        hline[lpos] = 0;
        puts_(hline);
        putc_('\n');
    }
    if (opened) sys_close(fd);
    return 0;
}
