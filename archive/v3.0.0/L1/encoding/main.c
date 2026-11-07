/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "encoding".
 *
 * One command = one app = one folder.  This app is the ring-3 half of
 * the v2.2.0 `encoding` command: the service body (numbered L1SVC_DISP_ENCODING)
 * lives in the L0 "display" service; the
 * call itself is the exact ush svc_cmd_run() semantics carried over
 * (256-byte argument line joined from argv, 4096-byte text result). */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_DISPLAY, L1SVC_DISP_ENCODING); }
