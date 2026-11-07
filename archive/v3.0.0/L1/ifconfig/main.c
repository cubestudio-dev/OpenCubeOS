/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ifconfig".
 * Moved verbatim from the ush `ifconfig` builtin: network interface info via the kernel
 * NETCMD bridge (SYS_NETCMD). */
#include "_shared/l1_lib.h"

int app_main(int argc, char **argv) {
    (void)argc;
    static char nout[4096];
    long rc = sys_netcmd(NETCMD_IFCONFIG, 0, nout, (long)sizeof(nout));
    if (rc < 0) {
        puts_(argv[0]);
        puts_(": kernel bridge unavailable\n");
        return 0;
    }
    puts_(nout);
    return 0;
}
