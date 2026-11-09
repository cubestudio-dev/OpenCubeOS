/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ssh".
 * The v2.2.0 `ssh` command.  Body lives in services/net
 * (L1SVC cat "net" op 13); the app
 * joins argv[1..] into the argument line and prints the service text -
 * the exact ush svc_cmd_run semantics. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_NET, L1SVC_NET_SSH); }
