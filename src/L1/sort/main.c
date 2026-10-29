/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "sort".
 * Moved verbatim from the ush `sort` builtin: reads argv[1] (P2-03 fix)
 * or stdin into a 16 KiB buffer (A16-12 full-capacity read fix), then
 * bubble-sorts up to 512 lines and prints them. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- sort (file or stdin) ---------- */
    static char sbuf[16384];
    static char *slines[512];
    int total = 0;
    int inpos = 0;
    long n;
    /* P2-03 FIX: if a filename argument is provided, read from the
     * file instead of stdin. */
    long src_fd = 0;
    int opened = 0;
    if (argc >= 2) {
        long fd = sys_open(argv[1], 1);  /* VFS_O_RDONLY (kernel flag 1 = read) */
        if (fd < 0) {
            puts_("sort: cannot open ");
            puts_(argv[1]);
            putc_('\n');
            return 0;
        }
        src_fd = fd;
        opened = 1;
    }
    /* A16-12 FIX: same 12288-B short-read as tail (see there):
     * `inpos < 16384 - 4096` silently capped sort at the first
     * 12 KiB of input. Read to the full buffer capacity instead. */
    while (inpos < (int)sizeof(sbuf) - 1 &&
           (n = sys_read(src_fd, sbuf + inpos, (int)sizeof(sbuf) - 1 - inpos)) > 0) {
        inpos += (int)n;
    }
    if (opened) sys_close(src_fd);
    sbuf[inpos] = 0;
    int start = 0;
    for (int i = 0; i < inpos; i++) {
        if (sbuf[i] == '\n') {
            sbuf[i] = 0;
            if (total < 512) slines[total++] = &sbuf[start];
            start = i + 1;
        }
    }
    if (start < inpos) {
        if (total < 512) slines[total++] = &sbuf[start];
    }
    /* Bubble sort (stable enough for shell use). */
    for (int a = 0; a < total - 1; a++) {
        for (int b = 0; b < total - a - 1; b++) {
            if (strcmp_(slines[b], slines[b + 1]) > 0) {
                char *tmp = slines[b];
                slines[b] = slines[b + 1];
                slines[b + 1] = tmp;
            }
        }
    }
    for (int a = 0; a < total; a++) {
        puts_(slines[a]);
        putc_('\n');
    }
    return 0;
}
