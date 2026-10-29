/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "df".
 * Moved verbatim from the ush `df` builtin: ring 3 cannot see the
 * kernel mount table, so it counts real VFS nodes/sizes under / via
 * sys_readdir/sys_stat and prints one ramfs row. */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* ---------- df ----------
     * P2-04 FIX: the user-space shell cannot access the kernel's VFS mount
     * table directly. Rather than printing fabricated "/ 64M 4M 60M 6%"
     * data, we report the real info we CAN observe from ring3 — for each
     * path that user-mode VFS exposes via sys_readdir/stat — and clearly
     * label that detailed mount info is only available from the kernel
     * shell's `df` command. */
    puts_("Filesystem     Mount     Size     Used    Avail  Use%\n");
    /* Walk root directory, count nodes/sizes (real VFS data). */
    int idx = 0;
    u64 total_bytes = 0;
    int file_count = 0;
    for (;;) {
        struct ush_dirent e;
        memset_(&e, 0, (int)sizeof(e));
        if (sys_readdir("/", idx, &e) < 0) break;
        if (e.name[0] == 0) break;
        file_count++;
        struct ush_stat st;
        memset_(&st, 0, (int)sizeof(st));
        char full[80];
        strncpy_(full, "/", 80);
        strcat_(full, e.name);
        if (sys_stat(full, &st) == 0) total_bytes += st.size;
        idx++;
        if (idx > 1024) break;
    }
    /* Print a single ramfs row reflecting the actual counted totals. */
    puts_("ramfs          /         ");
    putu_(total_bytes);
    puts_("        ");
    putu_((u64)file_count);
    puts_(" files (kernel df for full mount table)\n");
    return 0;
}
