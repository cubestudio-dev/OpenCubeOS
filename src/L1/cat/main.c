/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "cat".
 * Moved verbatim from the ush `cat` builtin: no argument copies stdin
 * to stdout, otherwise prints every argv[1..] file. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- cat [file...] ---------- */
    if (argc < 2) {
        static char cbuf[4096];
        long n;
        while ((n = sys_read(0, cbuf, 4096)) > 0) {
            sys_write(1, cbuf, n);
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        long fd = sys_open(argv[i], 1);
        if (fd < 0) {
            puts_("cat: cannot open ");
            puts_(argv[i]);
            putc_('\n');
            continue;
        }
        static char rbuf[4096];
        long n;
        while ((n = sys_read(fd, rbuf, 4096)) > 0) {
            sys_write(1, rbuf, n);
        }
        sys_close(fd);
    }
    return 0;
}
