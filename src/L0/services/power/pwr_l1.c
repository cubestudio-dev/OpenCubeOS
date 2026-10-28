/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/power L1 surface (SYS_L1SVC cat
 * "power"): the full power family for ring 3.  The recovery shell
 * keeps halt/shutdown/poweroff/reboot for its survival control -
 * both sides share the exact same command bodies (core_power.c,
 * main.c halt); there is exactly one power implementation. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "pwr_cmds.h"
#include "core_kcap.h"

typedef int (*pwr_cmd_fn_t)(const char *);

static int pwr_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    pwr_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_PWR_SHUTDOWN:      fn = pwr_cmd_shutdown; break;
        case L1SVC_PWR_REBOOT:        fn = pwr_cmd_reboot; break;
        case L1SVC_PWR_HALT:          fn = pwr_cmd_halt; break;
        case L1SVC_PWR_SLEEP:         fn = pwr_cmd_sleep; break;
        case L1SVC_PWR_POWEROFF_TEST: fn = pwr_cmd_poweroff_test; break;
        case L1SVC_PWR_SUSPEND_TEST:  fn = pwr_cmd_suspend_test; break;
        case L1SVC_PWR_HALT_TEST:     fn = pwr_cmd_halt_test; break;
        case L1SVC_PWR_REBOOT_TEST:   fn = pwr_cmd_reboot_test; break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: power service identity with its ring-3 surface.  Bring-up
 * (ACPI power module init) stays in kmain. */
OC_SERVICE_DECL_L1("power", "power service (shutdown/reboot/suspend + sleep + tests + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   pwr_l1_dispatch);
