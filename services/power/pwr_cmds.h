/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.2 - services/power command surface.
 *
 * halt/shutdown/poweroff/reboot/sleep command bodies live in
 * services/power/core_power.c; the test commands in pwr_cmds_test.c.
 */
#ifndef OC_PWR_CMDS_H
#define OC_PWR_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* core_power.c (v1 bodies, unchanged) */
int shell_cmd_shutdown(const char *args);
int shell_cmd_sleep(const char *args);
int shell_cmd_reboot(const char *args);

/* core/main.c */
int shell_cmd_halt(const char *args);

/* pwr_cmds_test.c (migrated) */
int pwr_cmd_poweroff_test(const char *args);
int pwr_cmd_suspend_test(const char *args);
int pwr_cmd_halt_test(const char *args);
int pwr_cmd_reboot_test(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_PWR_CMDS_H */
