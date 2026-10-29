/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "chown".
 * Moved verbatim from the ush `chown` builtin (WP-10-wp08fix1): parses
 * "<uid>[:<gid>]" in argv[1] and applies it to argv[2] via SYS_CHOWN. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- chown <uid>[:<gid>] <path> ---------- */
    if (argc < 3) {
        puts_("usage: chown <uid>[:<gid>] <path>\n");
        return 0;
    }
    u32 uid = 0, gid = 0;
    const char *s = argv[1];
    int got_uid = 0;
    while (*s >= '0' && *s <= '9') { uid = uid * 10 + (u32)(*s - '0'); s++; got_uid = 1; }
    if (*s == ':') {
        s++;
        while (*s >= '0' && *s <= '9') { gid = gid * 10 + (u32)(*s - '0'); s++; }
    }
    if (!got_uid || *s != 0) {
        puts_("usage: chown <uid>[:<gid>] <path>\n");
        return 0;
    }
    if (sys_chown(argv[2], (long)uid, (long)gid) < 0) {
        puts_("chown: cannot change owner\n");
    }
    return 0;
}
