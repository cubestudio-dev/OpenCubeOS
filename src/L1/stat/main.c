/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "stat".
 * Moved verbatim from the ush `stat` builtin (WP-10-wp08fix1): prints
 * the real fs_vfs_stat_t data (type/size/mode/uid/gid/nlink/name)
 * returned by SYS_STAT. */
#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- stat <path> (WP-10-wp08fix1) ----------
     * `help` has listed `stat` since WP-08 but no dispatcher entry ever
     * existed. Print the REAL fs_vfs_stat_t data (type/size/mode/uid/gid/
     * nlink/name) returned by SYS_STAT. */
    if (argc < 2) {
        puts_("usage: stat <path>\n");
        return 0;
    }
    struct ush_stat st;
    memset_(&st, 0, (int)sizeof(st));
    if (sys_stat(argv[1], &st) < 0) {
        puts_("stat: cannot stat '");
        puts_(argv[1]);
        puts_("'\n");
        return 0;
    }
    puts_("  File: ");
    puts_(st.name[0] ? st.name : argv[1]);
    putc_('\n');
    puts_("  Type: ");
    if (st.type == USH_TYPE_DIR) puts_("directory");
    else if (st.type == USH_TYPE_DEV) puts_("device");
    else puts_("regular file");
    putc_('\n');
    puts_("  Size: ");
    putu_(st.size);
    puts_(" bytes\n");
    /* mode in octal (e.g. 0600) */
    puts_("  Mode: ");
    {
        char mb[8];
        int mv = (int)(st.mode & 07777);
        mb[5] = 0;
        for (int d = 4; d >= 1; d--) { mb[d] = (char)('0' + (mv & 7)); mv >>= 3; }
        mb[0] = '0';   /* leading 0 for octal display */
        puts_(mb);
    }
    putc_('\n');
    puts_("  Uid:  ");
    putu_((u64)st.uid);
    puts_("  Gid: ");
    putu_((u64)st.gid);
    putc_('\n');
    puts_("  Links: ");
    putu_((u64)st.nlink);
    putc_('\n');
    return 0;
}
