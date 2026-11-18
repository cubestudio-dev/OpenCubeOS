/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - L1 app "drv": the driver-framework
 * diagnostics umbrella command (l1_diag_drv of the WP naming).
 * Implementation: 100% the shared layer in ocos/interface/l1_diag_cmds.h. */
#include "l1_diag_cmds.h"

int app_main(int argc, char **argv)
{
    return l1_diag_drv_cmd(argc, argv);
}
