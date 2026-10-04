/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d-fix2
 * File: kernel/power_test_cmds.h
 * Purpose: shell test suite for power management + structured help.
 */
#ifndef OC_POWER_TEST_CMDS_H
#define OC_POWER_TEST_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register poweroff_test / suspend_test / halt_test / reboot_test /
 * help_default_test / help_wp_test / help_a_test.  Called once at boot
 * from kmain. */
void power_test_cmds_register(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_POWER_TEST_CMDS_H */
