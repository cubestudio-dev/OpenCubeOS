/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/ota L1 surface (SYS_L1SVC cat "ota"):
 * 16 ops (the update/config family; `reboot` belongs to power).
 * Bodies: ota_ab.c, ota_update.c, core/lib/lib_config.c,
 * ota_cmds_test.c - executed captured. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "ota_cmds.h"
#include "shell.h"

typedef int (*ota_cmd_fn_t)(const char *);

static int ota_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    ota_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_OTA_CHECKUPDATE:      fn = shell_cmd_checkupdate; break;
        case L1SVC_OTA_CONFIG:           fn = shell_cmd_config; break;
        case L1SVC_OTA_CONFIG_TEST:      fn = shell_cmd_config_test; break;
        case L1SVC_OTA_CHECKUPDATE_TEST: fn = shell_cmd_checkupdate_test; break;
        case L1SVC_OTA_UPDATE:           fn = shell_cmd_update; break;
        case L1SVC_OTA_ROLLBACK:         fn = shell_cmd_rollback; break;
        case L1SVC_OTA_PKG_TEST:         fn = ota_cmd_update_pkg_test; break;
        case L1SVC_OTA_AB_TEST:          fn = ota_cmd_ab_partition_test; break;
        case L1SVC_OTA_CHECK_TEST:       fn = ota_cmd_update_check_test; break;
        case L1SVC_OTA_DOWNLOAD_TEST:    fn = ota_cmd_update_download_test; break;
        case L1SVC_OTA_VERIFY_TEST:      fn = ota_cmd_update_verify_test; break;
        case L1SVC_OTA_INSTALL_TEST:     fn = ota_cmd_update_install_test; break;
        case L1SVC_OTA_ROLLBACK_TEST:    fn = ota_cmd_update_rollback_test; break;
        case L1SVC_OTA_LOCAL_TEST:       fn = ota_cmd_update_local_test; break;
        case L1SVC_OTA_STATUS_TEST:      fn = ota_cmd_update_status_test; break;
        case L1SVC_OTA_REAL_TEST:        fn = ota_cmd_real_update_test; break;
        default: return -1;
    }
    return shell_run_fn_captured(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: ota service identity with its ring-3 surface.  Bring-up:
 * none at collect time (ota_update_confirm_boot / the async check
 * run from kmain); the service exists for its operations. */
OC_SERVICE_DECL_L1("ota", "update service (A/B slots + update check + install + config store + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   ota_l1_dispatch);
