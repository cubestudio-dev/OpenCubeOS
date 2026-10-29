/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - L1 app "ldd".
 * The v2.2.0 `ldd <program>` command: PT_INTERP + DT_NEEDED introspection
 * over the OC_USERPROGS registry (the 20-name hardcoded chain is gone).
 * Body lives in services/l1 (L1SVC cat "l1" op 3). */
#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"

int app_main(int argc, char **argv) { return l1svc_main(argc, argv, L1SVC_CAT_L1, L1SVC_L1_LDD); }
