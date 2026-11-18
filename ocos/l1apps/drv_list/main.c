/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - L1 app "drv_list": the standalone twin
 * of `drv list` (l1_diag_drv_sub of the WP naming).  Same shared
 * layer, same code path, byte-identical output. */
#include "l1_diag_cmds.h"

int app_main(int argc, char **argv)
{
    return l1_diag_drv_sub(argc, argv, "list");
}
