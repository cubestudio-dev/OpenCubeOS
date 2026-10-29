/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "sed".
 * Moved verbatim from the ush `sed` builtin: s/old/new/[g], -n Np and Nd
 * scripts over stdin or a trailing file argument. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- sed s/old/new/[g] | -n Np | Nd [file] ---------- */
    if (argc < 2) {
        puts_("usage: sed s/old/new/[g] [file] | sed -n Np [file] | sed Nd [file]\n");
        return 0;
    }
    int src_fd = 0;
    int opened = 0;
    int script_idx = 1;
    if (argc >= 3) {
        /* last arg may be a file (when it is not part of the script) */
        if (streq_(argv[1], "-n") && argc >= 4) {
            script_idx = 2;
        }
        /* try the last argument as a file name */
        long fd = sys_open(argv[argc - 1], 1);
        if (fd >= 0) {
            src_fd = (int)fd;
            opened = 1;
        }
    }
    static char sbuf[16384];
    long total = 0;
    long n;
    /* A16-12 FIX: same 12288-B short-read as tail (see there); the
     * old condition also performed the read BEFORE the capacity
     * check, so a full final read let `sbuf[total] = 0` touch
     * sbuf[16384] — 1 byte past the buffer. Capacity is checked
     * first and each read asks for exactly the remaining space. */
    while (total < (long)sizeof(sbuf) - 1 &&
           (n = sys_read(src_fd, sbuf + total, (long)sizeof(sbuf) - 1 - total)) > 0) {
        total += n;
    }
    if (opened) sys_close(src_fd);
    sbuf[total] = 0;

    const char *script = argv[script_idx];
    int lineno = 0;
    int i = 0;
    while (i <= total) {
        /* find one line */
        int ls = i;
        while (i < total && sbuf[i] != '\n') i++;
        int le = i;
        if (i < total) i++;      /* skip \n */
        int len = le - ls;
        if (ls == le && i > total) break;
        lineno++;
        (void)len;

        if (script[0] == '-' && script[1] == 'n') {
            script++;
        }
        if (script[0] == '-' && script[1] == 'n') {
            /* -n inside argv[1] handled above; skip */
        }
        const char *sc = argv[script_idx];
        if (sc[0] == '-' && sc[1] == 'n') sc += 2;
        if (sc[0] == 's' && sc[1] == '/') {
            /* s/old/new/[g] */
            char pat[128], rep[128];
            int pi = 0;
            const char *p = sc + 2;
            while (*p && *p != '/' && pi < 127) pat[pi++] = *p++;
            pat[pi] = 0;
            if (*p == '/') p++;
            int ri = 0;
            while (*p && *p != '/' && ri < 127) rep[ri++] = *p++;
            rep[ri] = 0;
            int global = 0;
            if (*p == '/') p++;
            if (*p == 'g') global = 1;
            /* emit line with replacements */
            int j = ls;
            while (j < le) {
                int m = 0;
                while (m < pi && j + m < le && sbuf[j + m] == pat[m]) m++;
                if (pi > 0 && m == pi) {
                    sys_write(1, rep, ri);
                    if (!global) {
                        /* rest of the line verbatim */
                        sys_write(1, sbuf + j + pi, le - (j + pi));
                        break;
                    }
                    j += pi;
                } else {
                    putc_(sbuf[j]);
                    j++;
                }
            }
            putc_('\n');
        } else if (sc[0] == 'd') {
            int num = (int)atoi_(sc + 1);
            if (num == lineno) {
                /* delete: print nothing */
            } else {
                sys_write(1, sbuf + ls, le - ls);
                putc_('\n');
            }
        } else if (sc[0] == 'p' || (sc[0] >= '0' && sc[0] <= '9')) {
            /* [N]p - print only line N */
            int num = 0;
            int k2 = 0;
            while (sc[k2] >= '0' && sc[k2] <= '9') { num = num * 10 + (sc[k2] - '0'); k2++; }
            if (sc[k2] == 'p') {
                if (num == lineno) {
                    sys_write(1, sbuf + ls, le - ls);
                    putc_('\n');
                }
            }
        } else {
            puts_("sed: unknown script\n");
            return 0;
        }
        if (i > total) break;
        if (ls == le && i >= total) break;
    }
    return 0;
}
