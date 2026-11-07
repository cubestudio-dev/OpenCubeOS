/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "l1test".
 * The v2.2.0 `l1test` command: job interface self-test.  Body lives in
 * services/l1 (L1SVC cat "l1" op 2). */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv) { return l1svc_main(argc, argv, L1SVC_CAT_L1, L1SVC_L1_L1TEST); }
