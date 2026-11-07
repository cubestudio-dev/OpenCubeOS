/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "smbus".
 * the v2.2.0 `smbus` command (SMBus device list).  Body lives in services/diag
 * (L1SVC cat "diag" op 38); the app joins argv[1..] into the
 * argument line and prints the service text - the exact ush
 * svc_cmd_run semantics. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_DIAG, L1SVC_DIAG_SMBUS); }
