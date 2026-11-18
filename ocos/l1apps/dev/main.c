/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - L1 app "dev": the device-tree
 * diagnostics umbrella command (l1_diag_dev of the WP naming).
 * Implementation: 100% the shared layer in ocos/interface/l1_diag_cmds.h. */
#include "l1_diag_cmds.h"

int app_main(int argc, char **argv)
{
    return l1_diag_dev_cmd(argc, argv);
}
