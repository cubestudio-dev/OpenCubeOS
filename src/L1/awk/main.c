/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "awk".
 * Moved verbatim from the ush `awk` builtin: {print $N|NF|NR} with an
 * optional /pat/ filter and an optional trailing file argument. */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- awk '{print $N | NF | NR}' [/pat/] [file] ---------- */
    if (argc < 2) {
        puts_("usage: awk '{print $N|NF|NR}' [/pat/] [file]\n");
        return 0;
    }
    const char *prog = argv[1];
    /* accept both quoted '{print $1}' and bare {print $1} */
    while (*prog && *prog != '{') prog++;
    if (*prog != '{') {
        puts_("awk: missing {print ...}\n");
        return 0;
    }
    prog++;
    while (*prog == ' ') prog++;
    if (strncmp_(prog, "print", 5) != 0) {
        puts_("awk: only {print ...} is supported\n");
        return 0;
    }
    prog += 5;
    while (*prog == ' ') prog++;
    char field_spec[8];
    int fs = 0;
    while (*prog && *prog != '}' && *prog != ' ' && fs < 7) {
        field_spec[fs++] = *prog++;
    }
    field_spec[fs] = 0;
    const char *pattern = 0;
    if (argc >= 3 && argv[2][0] == '/') {
        /* A16-13 FIX: a trailing '/' used to disable the whole
         * filter (`/apple/` set pattern=0 and was then treated as
         * the FILE argument -> "awk: cannot open /apple/"). Strip
         * the trailing '/' and keep filtering; a bare "/" leaves
         * an empty pattern, which strstr_ always matches (same as
         * the old bare-/ behavior). */
        pattern = argv[2] + 1;
        int pl = (int)strlen_(pattern);
        if (pl > 0 && pattern[pl - 1] == '/') {
            /* BUG-0251 FIX: NUL-terminate at the trailing '/'
             * ITSELF. `pattern` is argv[2]+1, so its last char
             * (the '/') sits at argv[2][pl] == pattern[pl-1]. The
             * old write landed at argv[2][pl-1] == pattern[pl-2] -
             * one byte EARLY - cutting the final character of the
             * pattern: host-C simulation showed "/apple/"
             * filtering on "appl" and "/x/" becoming an EMPTY
             * pattern that matched every line. */
            argv[2][pl] = 0;
        }
    }
    int file_arg = (pattern ? 3 : 2);
    long src_fd = 0;
    int opened = 0;
    if (argc > file_arg) {
        long fd = sys_open(argv[file_arg], 1);
        if (fd < 0) {
            puts_("awk: cannot open ");
            puts_(argv[file_arg]);
            putc_('\n');
            return 0;
        }
        src_fd = fd;
        opened = 1;
    }
    static char abuf[8192];
    static char aline[1024];
    int lpos = 0;
    int nr = 0;
    long n;
    /* helper macro-ish code: print requested field of aline */
    while ((n = sys_read(src_fd, abuf, 8192)) > 0) {
        for (int i2 = 0; i2 < (int)n; i2++) {
            char c = abuf[i2];
            if (c == '\n') {
                aline[lpos] = 0;
                nr++;
                /* pattern filter */
                int show = 1;
                if (pattern && !strstr_(aline, pattern)) show = 0;
                if (show) {
                    if (streq_(field_spec, "NR")) {
                        putu_((u64)nr);
                        putc_('\n');
                    } else if (streq_(field_spec, "NF")) {
                        int nf = 0;
                        int in_w = 0;
                        for (int q = 0; aline[q]; q++) {
                            if (aline[q] == ' ' || aline[q] == '\t') in_w = 0;
                            else if (!in_w) { in_w = 1; nf++; }
                        }
                        putu_((u64)nf);
                        putc_('\n');
                    } else if (field_spec[0] == '$') {
                        int want = (int)atoi_(field_spec + 1);
                        int fno = 0;
                        int q = 0;
                        while (aline[q] == ' ' || aline[q] == '\t') q++;
                        while (aline[q] && fno <= want) {
                            int ws = q;
                            while (aline[q] && aline[q] != ' ' && aline[q] != '\t') q++;
                            fno++;
                            if (fno == want) {
                                sys_write(1, aline + ws, q - ws);
                                break;
                            }
                            while (aline[q] == ' ' || aline[q] == '\t') q++;
                        }
                        putc_('\n');
                    } else {
                        puts_(aline);
                        putc_('\n');
                    }
                }
                lpos = 0;
            } else {
                if (lpos < 1023) aline[lpos++] = c;
            }
        }
    }
    if (opened) sys_close(src_fd);
    return 0;
}
