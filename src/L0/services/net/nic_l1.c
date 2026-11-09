/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/nic L1 surface.
 *
 * SYS_L1SVC entry for the NIC service: 18 NIC ops + the wireless
 * family (wifi/bt) ride the "nic" category - the wireless drivers are
 * NICs from the service taxonomy's point of view (their commands'
 * v1 group tag was "wireless", the op names carry the family).
 * Command bodies: services/nic/nic_cmds.c / wl_cmds.c / wl_cmds_bt.c,
 * executed captured.
 */
#include "nic_svc.h"
#include "nic_drv_internal.h"
#include "core_registry.h"
#include "l1svc_defs.h"
#include "wl_cmds.h"
#include "core_kcap.h"

typedef int (*nic_cmd_fn_t)(const char *);

static int nic_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    nic_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_NIC_E1000E:        fn = nic_cmd_nic_e1000e; break;
        case L1SVC_NIC_IGB:           fn = nic_cmd_nic_igb; break;
        case L1SVC_NIC_IXGBE:         fn = nic_cmd_nic_ixgbe; break;
        case L1SVC_NIC_RTL8139:       fn = nic_cmd_nic_rtl8139; break;
        case L1SVC_NIC_RTL8168:       fn = nic_cmd_nic_rtl8168; break;
        case L1SVC_NIC_RTL8125:       fn = nic_cmd_nic_rtl8125; break;
        case L1SVC_NIC_RTL810X:       fn = nic_cmd_nic_rtl810x; break;
        case L1SVC_NIC_BCM57XX:       fn = nic_cmd_nic_bcm57xx; break;
        case L1SVC_NIC_E1000E_TEST:   fn = nic_cmd_e1000e_test; break;
        case L1SVC_NIC_IGB_TEST:      fn = nic_cmd_igb_test; break;
        case L1SVC_NIC_IXGBE_TEST:    fn = nic_cmd_ixgbe_test; break;
        case L1SVC_NIC_RTL8139_TEST:  fn = nic_cmd_rtl8139_test; break;
        case L1SVC_NIC_RTL8168_TEST:  fn = nic_cmd_rtl8168_test; break;
        case L1SVC_NIC_RTL8125_TEST:  fn = nic_cmd_rtl8125_test; break;
        case L1SVC_NIC_RTL810X_TEST:  fn = nic_cmd_rtl810x_test; break;
        case L1SVC_NIC_BCM57XX_TEST:  fn = nic_cmd_bcm57xx_test; break;
        case L1SVC_NIC_OTHER_TEST:    fn = nic_cmd_other_nic_test; break;
        case L1SVC_NIC_RW_TEST:       fn = nic_cmd_nic_rw_test; break;
        case L1SVC_NIC_WIFI:          fn = wl_cmd_wifi; break;
        case L1SVC_NIC_BT:            fn = wl_cmd_bt; break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: the nic service identity now carries its ring-3 surface
 * (init/late unchanged from the group D declaration). */
OC_SERVICE_DECL_L1("nic", "NIC service (card table + dispatch + probe collection + wireless family + ring-3 surface)",
                   nic_svc_init, nic_svc_probe_all,
                   nic_l1_dispatch);
