/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ping".
 * Moved verbatim from the ush `ping` builtin: ICMP echo via the kernel
 * NETCMD bridge (SYS_NETCMD op 2). */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    /* ---------- ping <host> (kernel ICMP via NETCMD bridge) ---------- */
    if (argc < 2) {
        puts_("usage: ping <host>\n");
        return 0;
    }
    static char nout[4096];
    long rc = sys_netcmd(NETCMD_PING, argv[1], nout, (long)sizeof(nout));
    if (rc < 0) {
        puts_("ping: kernel bridge unavailable\n");
        return 0;
    }
    puts_(nout);
    return 0;
}
