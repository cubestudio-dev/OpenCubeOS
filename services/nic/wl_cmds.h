/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/nic + wireless command surface.
 *
 * Exported command functions migrated verbatim from recovery/
 * (shell_cmds_nic_test.c, shell_cmds_wireless.c, shell_cmds_bt.c).
 * Bodies unchanged; invoked captured via SYS_L1SVC (nic/wireless ops).
 */
#ifndef OC_NIC_WL_CMDS_H
#define OC_NIC_WL_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* nic_cmds.c (from recovery/shell_cmds_nic_test.c) - status */
int nic_cmd_nic_e1000e(const char *args);
int nic_cmd_nic_igb(const char *args);
int nic_cmd_nic_ixgbe(const char *args);
int nic_cmd_nic_rtl8139(const char *args);
int nic_cmd_nic_rtl8168(const char *args);
int nic_cmd_nic_rtl8125(const char *args);
int nic_cmd_nic_rtl810x(const char *args);
int nic_cmd_nic_bcm57xx(const char *args);
/* nic_cmds.c - tests */
int nic_cmd_e1000e_test(const char *args);
int nic_cmd_igb_test(const char *args);
int nic_cmd_ixgbe_test(const char *args);
int nic_cmd_rtl8139_test(const char *args);
int nic_cmd_rtl8168_test(const char *args);
int nic_cmd_rtl8125_test(const char *args);
int nic_cmd_rtl810x_test(const char *args);
int nic_cmd_bcm57xx_test(const char *args);
int nic_cmd_other_nic_test(const char *args);
int nic_cmd_nic_rw_test(const char *args);

/* wl_cmds.c / wl_cmds_bt.c (wireless family) */
int wl_cmd_wifi(const char *args);
int wl_cmd_bt(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_NIC_WL_CMDS_H */
