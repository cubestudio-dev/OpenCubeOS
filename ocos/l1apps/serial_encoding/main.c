/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g3 - L1 app "serial_encoding".
 *
 * One command = one app = one folder.  The service body (numbered
 * L1SVC_DISP_SERIAL_ENC) lives in the L0 "display" service
 * (services/display/serial_enc.c); this app is the ring-3 half that
 * forwards the argument line and relays the captured text - the exact
 * ush svc_cmd_run() semantics (256-byte argument line, 4096-byte
 * text result). */
#include "l1_lib.h"
#include "l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_DISPLAY, L1SVC_DISP_SERIAL_ENC); }
