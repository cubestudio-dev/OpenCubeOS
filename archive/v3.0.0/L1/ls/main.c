/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ls".
 * Moved verbatim from the ush `ls` builtin: lists the given directory
 * (or the cwd) one entry per line, tagged d/c/- by VFS node type. */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- ls [path] ---------- */
    char path[256];
    if (argc > 1) {
        strncpy_(path, argv[1], 256);
    } else {
        sys_getcwd(path, 256);
    }
    int idx = 0;
    for (;;) {
        struct ush_dirent e;
        memset_(&e, 0, (int)sizeof(e));
        if (sys_readdir(path, idx, &e) < 0) break;
        if (e.name[0] == 0) break;
        char tag = '-';
        if (e.type == USH_TYPE_DIR) tag = 'd';
        else if (e.type == USH_TYPE_DEV) tag = 'c';
        putc_(tag);
        putc_(' ');
        puts_(e.name);
        putc_('\n');
        idx++;
        if (idx > 1024) break;
    }
    return 0;
}
