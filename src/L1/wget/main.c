/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "wget".
 * Moved verbatim from the ush `wget` builtin: kernel HTTP client via
 * the NETCMD bridge (SYS_NETCMD op 4), host [port] [path] packed into
 * one space-joined 256-byte line. */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- wget <host> [port] [path] ---------- */
    if (argc < 2) {
        puts_("usage: wget <host> [port] [path]\n");
        return 0;
    }
    static char arg[256];
    arg[0] = 0;
    int used = 0;
    for (int i = 1; i < argc && i <= 3; i++) {
        if (i > 1) arg[used++] = ' ';
        int al = (int)strlen_(argv[i]);
        for (int q = 0; q < al && used < 250; q++) arg[used++] = argv[i][q];
    }
    arg[used] = 0;
    static char nout[4096];
    long rc = sys_netcmd(NETCMD_WGET, arg, nout, (long)sizeof(nout));
    if (rc < 0) {
        puts_("wget: kernel bridge unavailable\n");
        return 0;
    }
    puts_(nout);
    return 0;
}
