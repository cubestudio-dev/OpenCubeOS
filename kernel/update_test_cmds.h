/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/update_test_cmds.h
 * Purpose: shell test suite for the in-system update (A/B partitions).
 */
#ifndef OC_UPDATE_TEST_CMDS_H
#define OC_UPDATE_TEST_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Shell command handlers (also registered via update_test_cmds_register). */
int cmd_update_pkg_test(const char *args);
int cmd_ab_partition_test(const char *args);
int cmd_update_check_test(const char *args);
int cmd_update_download_test(const char *args);
int cmd_update_verify_test(const char *args);
int cmd_update_install_test(const char *args);
int cmd_update_rollback_test(const char *args);
int cmd_update_local_test(const char *args);
int cmd_update_status_test(const char *args);
int cmd_real_update_test(const char *args);

/* Registers every WP-10u test command with the shell. */
void update_test_cmds_register(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_UPDATE_TEST_CMDS_H */
