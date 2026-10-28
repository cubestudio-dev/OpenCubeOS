/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/power op surface.
 *
 * shutdown/poweroff/reboot/sleep op bodies live in
 * services/power/core_power.c; halt in pwr_cmds_kern.c (rescued from
 * core/main.c when the oc> shell died); the test ops in
 * pwr_cmds_test.c.  All renamed shell_cmd_* -> pwr_cmd_* in WP-03.
 */
#ifndef OC_PWR_CMDS_H
#define OC_PWR_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* core_power.c (v1 bodies, renamed pwr_cmd_* in WP-03) */
int pwr_cmd_shutdown(const char *args);
int pwr_cmd_sleep(const char *args);
int pwr_cmd_reboot(const char *args);

/* pwr_cmds_kern.c (rescued from core/main.c) */
int pwr_cmd_halt(const char *args);

/* pwr_cmds_test.c (migrated) */
int pwr_cmd_poweroff_test(const char *args);
int pwr_cmd_suspend_test(const char *args);
int pwr_cmd_halt_test(const char *args);
int pwr_cmd_reboot_test(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_PWR_CMDS_H */
