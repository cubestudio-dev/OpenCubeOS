/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "tail".
 * Moved verbatim from the ush `tail` builtin: prints the last 10
 * lines (or `-N`/`N`, capped at 512) of stdin or the file argument. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- tail [N] [file] ---------- */
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
    if (nlines > 512) nlines = 512;
    long fd = 0;
    int opened = 0;
    if (argi < argc) {
        fd = sys_open(argv[argi], 1);
        if (fd < 0) {
            puts_("tail: cannot open ");
            puts_(argv[argi]);
            putc_('\n');
            return 0;
        }
        opened = 1;
    }
    static char tbuf[16384];
    static char *tlines[512];
    int total = 0;
    int inpos = 0;
    long n;
    /* A16-12 FIX: the old condition `inpos < 16384 - 4096` stopped
     * the loop at 12288 B (the && short-circuit ate the final read),
     * so tail only ever saw the first 12 KiB of its 16 KiB buffer.
     * Read until the buffer is genuinely full (or EOF), requesting
     * exactly the remaining space; the capacity is checked BEFORE
     * the read, so the NUL below can never land 1 past the end. */
    while (inpos < (int)sizeof(tbuf) - 1 &&
           (n = sys_read(fd, tbuf + inpos, (int)sizeof(tbuf) - 1 - inpos)) > 0) {
        inpos += (int)n;
    }
    tbuf[inpos] = 0;
    int start = 0;
    for (int i = 0; i < inpos; i++) {
        if (tbuf[i] == '\n') {
            tbuf[i] = 0;
            if (total < 512) tlines[total++] = &tbuf[start];
            start = i + 1;
        }
    }
    if (start < inpos) {
        if (total < 512) tlines[total++] = &tbuf[start];
    }
    int s = total > nlines ? total - nlines : 0;
    for (int i = s; i < total; i++) {
        puts_(tlines[i]);
        putc_('\n');
    }
    if (opened) sys_close(fd);
    return 0;
}
