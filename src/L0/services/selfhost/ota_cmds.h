/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/ota command surface.
 *
 * Bodies live in ota_ab.c / ota_update.c (already in services/ota),
 * core/lib/lib_config.c (config family) and ota_cmds_test.c
 * (migrated from recovery/shell_cmds_update_test.c).
 */
#ifndef OC_OTA_CMDS_H
#define OC_OTA_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ota_update.c */
int ota_cmd_checkupdate(const char *args);
int ota_cmd_checkupdate_test(const char *args);
/* ota_ab.c */
int ota_cmd_update(const char *args);
int ota_cmd_rollback(const char *args);
/* core/lib/lib_config.c */
int ota_cmd_config(const char *args);
int ota_cmd_config_test(const char *args);
/* ota_cmds_test.c (migrated) */
int ota_cmd_update_pkg_test(const char *args);
int ota_cmd_ab_partition_test(const char *args);
int ota_cmd_update_check_test(const char *args);
int ota_cmd_update_download_test(const char *args);
int ota_cmd_update_verify_test(const char *args);
int ota_cmd_update_install_test(const char *args);
int ota_cmd_update_rollback_test(const char *args);
int ota_cmd_update_local_test(const char *args);
int ota_cmd_update_status_test(const char *args);
int ota_cmd_real_update_test(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_OTA_CMDS_H */
