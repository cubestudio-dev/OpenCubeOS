/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "netstat".
 * Moved verbatim from the ush `netstat` builtin: network statistics via the kernel
 * NETCMD bridge (SYS_NETCMD).  WP-runtime-hardening i18n: the failure
 * tail renders through tr_() (argv[0] prefix stays the command word). */
#include "l1_api_syscall.h"

int app_main(int argc, char **argv) {
    (void)argc;
    static char nout[4096];
    long rc = sys_netcmd(NETCMD_NETSTAT, 0, nout, (long)sizeof(nout));
    if (rc < 0) {
        puts_(argv[0]);
        puts_(tr_(": kernel bridge unavailable\n"));
        return 0;
    }
    puts_(nout);
    return 0;
}
