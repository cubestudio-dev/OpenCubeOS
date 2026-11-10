/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "write".
 * Moved verbatim from the ush `write` builtin (the v1 oc> write command
 * as a file syscall tool): writes argv[2..] joined by spaces + newline
 * into argv[1].
 * BUG-35 FIX (R3-H): the app had been a silent no-op since release -
 * it opened argv[1] with flag 1 = VFS_O_RDONLY (the "O_WRONLY|O_CREAT
 * as v1" comment was wrong; fs_vfs.h: RDONLY=0x1, WRONLY=0x2, CREAT=0x4,
 * TRUNC=0x10) and ignored every failing sys_write.  Now: write-mode
 * flags (22 = VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC, the same value the
 * cp/mv copy targets and the ush `>` redirect use) and every sys_write
 * return is checked - a failure prints a clear error and exits 1.
 * The success path is unchanged. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* v2.0.0: the v1 oc> `write <path> <text>` command, as a ush
     * builtin through the classic file syscalls. */
    if (argc < 3) { puts_("usage: write <path> <text>\n"); return 0; }
    /* BUG-35 FIX (R3-H): was `sys_open(argv[1], 1)` - flag 1 is
     * VFS_O_RDONLY, so every write below failed.  22 = VFS_O_WRONLY |
     * VFS_O_CREAT | VFS_O_TRUNC (fs_vfs.h: 0x2|0x4|0x10), as v1 meant. */
    long fd = sys_open(argv[1], 22 /* VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC */);
    if (fd < 0) { puts_("write: cannot open "); puts_(argv[1]); putc_('\n'); return 1; }
    for (int i = 2; i < argc; i++) {
        /* BUG-35 FIX (R3-H): check every write - a short or negative
         * return used to pass silently (the silent no-op). */
        long n = strlen_(argv[i]);
        if ((i > 2 && sys_write(fd, " ", 1) != 1) ||
            (n > 0 && sys_write(fd, argv[i], n) != n)) {
            puts_("write: write failed "); puts_(argv[1]); putc_('\n');
            sys_close(fd);
            return 1;
        }
    }
    if (sys_write(fd, "\n", 1) != 1) {
        puts_("write: write failed "); puts_(argv[1]); putc_('\n');
        sys_close(fd);
        return 1;
    }
    sys_close(fd);
    return 0;
}
