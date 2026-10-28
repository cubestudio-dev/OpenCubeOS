/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/input L1 surface (SYS_L1SVC cat
 * "input"): the touch/touchpad family.  Keyboard bring-up stays in
 * kmain (it is on the boot critical path). */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "disp_cmds.h"
#include "core_kcap.h"

typedef int (*inp_cmd_fn_t)(const char *);

static int inp_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    inp_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_INP_TOUCHPAD:  fn = inp_cmd_touchpad; break;
        case L1SVC_INP_TOUCHTEST: fn = inp_cmd_touchtest; break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

OC_SERVICE_DECL_L1("input", "input service (keyboard + touch + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   inp_l1_dispatch);
