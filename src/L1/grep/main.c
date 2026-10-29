/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "grep".
 * Moved verbatim from the ush `grep` builtin: filters stdin (fd 0,
 * pattern only - the builtin never opened a file argument) printing
 * every line that contains argv[1]. */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- grep <pattern> ---------- */
    if (argc < 2) {
        puts_("usage: grep <pattern>\n");
        return 0;
    }
    const char *pat = argv[1];
    static char gbuf[8192];
    static char gline[1024];
    int lpos = 0;
    long n;
    while ((n = sys_read(0, gbuf, 8192)) > 0) {
        for (int i = 0; i < (int)n; i++) {
            char c = gbuf[i];
            if (c == '\n') {
                gline[lpos] = 0;
                if (strstr_(gline, pat)) {
                    puts_(gline);
                    putc_('\n');
                }
                lpos = 0;
            } else {
                if (lpos < 1023) gline[lpos++] = c;
            }
        }
    }
    if (lpos > 0) {
        gline[lpos] = 0;
        if (strstr_(gline, pat)) {
            puts_(gline);
            putc_('\n');
        }
    }
    return 0;
}
