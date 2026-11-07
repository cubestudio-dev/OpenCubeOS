/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/power: halt op body, rescued
 * verbatim from core/main.c (renamed shell_cmd_halt -> pwr_cmd_halt
 * when the oc> shell died). */
#include "pwr_cmds.h"
#include "core_power.h"

int pwr_cmd_halt(const char *args) {
    (void)args;
    /* WP-10d-fix2: halt moved into kernel/power.c (cli + hlt loop with
     * the "System halted." banner). */
    core_power_halt();
    return 0; /* unreachable */
}
