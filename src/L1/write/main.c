/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "write".
 * Moved verbatim from the ush `write` builtin (the v1 oc> write command
 * as a file syscall tool): writes argv[2..] joined by spaces + newline
 * into argv[1] (O_CREAT, no truncate - as the shell builtin did). */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* v2.0.0: the v1 oc> `write <path> <text>` command, as a ush
     * builtin through the classic file syscalls. */
    if (argc < 3) { puts_("usage: write <path> <text>\n"); return 0; }
    long fd = sys_open(argv[1], 1 /* O_WRONLY|O_CREAT as v1 */);
    if (fd < 0) { puts_("write: cannot open "); puts_(argv[1]); putc_('\n'); return 0; }
    for (int i = 2; i < argc; i++) {
        if (i > 2) sys_write(fd, " ", 1);
        sys_write(fd, argv[i], strlen_(argv[i]));
    }
    sys_write(fd, "\n", 1);
    sys_close(fd);
    return 0;
}
