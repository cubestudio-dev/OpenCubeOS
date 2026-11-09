/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "tui-time".
 *
 * One command = one app = one folder.  This app is the ring-3 half of
 * the v2.2.0 `tui-time` command: the service body (numbered L1SVC_TUI_TIME) lives in
 * the L0 "tui" service (revived in G11c: bodies in services/tui/tui_cmd.c,
 * dispatch in tui_l1.c); the call itself is the exact ush svc_cmd_run()
 * semantics carried over (256-byte argument line joined from argv,
 * 4096-byte text result).  The full-screen page renders live on the
 * console/framebuffer (the op routes directly, bypassing the text
 * capture); tui-test serves its verdict block through the capture. */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_TUI, L1SVC_TUI_TIME); }
