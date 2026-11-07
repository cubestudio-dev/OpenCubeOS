/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "du".
 * Moved verbatim from the ush `du` builtin (WP-10-wp08fix1): recursive
 * depth-first walk summing file sizes under argv[1] (or the cwd). */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- du [path] (WP-10-wp08fix1: recursive) ---------- */
    char path[256];
    if (argc > 1) {
        strncpy_(path, argv[1], 256);
    } else {
        sys_getcwd(path, 256);
    }
    /* Recursive walker: sums file sizes under path. */
    static u64 du_total;
    static long du_files;
    du_total = 0;
    du_files = 0;
    /* Iterative stack of (path) strings - depth-first walk. */
    static char stack[16][256];
    int sp = 0;
    strncpy_(stack[sp++], path, 256);
    while (sp > 0) {
        char cur[256];
        strncpy_(cur, stack[--sp], 256);
        int idx = 0;
        for (;;) {
            struct ush_dirent e;
            memset_(&e, 0, (int)sizeof(e));
            if (sys_readdir(cur, idx, &e) < 0) break;
            if (e.name[0] == 0) break;
            char full[512];
            strncpy_(full, cur, 500);
            int fl = (int)strlen_(full);
            if (fl > 0 && full[fl - 1] != '/' && fl < 500) full[fl++] = '/';
            full[fl] = 0;
            strcat_(full, e.name);
            struct ush_stat st;
            memset_(&st, 0, (int)sizeof(st));
            if (sys_stat(full, &st) == 0) {
                if (st.type == USH_TYPE_DIR) {
                    if (sp < 16) {
                        strncpy_(stack[sp++], full, 256);
                    }
                } else {
                    du_total += st.size;
                    du_files++;
                }
            }
            idx++;
            if (idx > 1024) break;
        }
    }
    putu_(du_total);
    puts_(" bytes  ");
    putu_((u64)du_files);
    puts_(" files  ");
    puts_(path);
    putc_('\n');
    return 0;
}
