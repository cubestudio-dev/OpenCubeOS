/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/nic: driver-internal interface.
 *
 * Kernel-internal ONLY: NIC drivers (the NIC driver files, the wireless
 * bridge and the netfilter/netmap virtual cards) include this header
 * to add a probed card to the NIC service table.  NOT part of the L1
 * surface: the register-class entry was removed in v2.0.0; drivers
 * declare identity with OC_DRIVER_DECL("nic", ...) and call
 * nic_dev_add() from inside their probe.
 */
#ifndef OC_NIC_DRV_INTERNAL_H
#define OC_NIC_DRV_INTERNAL_H

#include "nic_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probe descriptor each nic driver declares in its own file:
 *   static const nic_drv_ops_t g_probe = { my_probe };
 *   OC_DRIVER_DECL("nic", "e1000e", &g_probe, "Intel E1000E");
 * my_probe() scans the bus, inits the first match and adds the card
 * via nic_dev_add().  Returns 0 when a card was added. */
typedef struct {
    int (*probe)(void);   /* 0 = a card was found and added */
} nic_drv_ops_t;

/* Add a probed card to the NIC service table (was the L1-surface
 * driver_nic_register(); internal since v2.0.0). */
int nic_dev_add(driver_nic_device_t *dev, const driver_nic_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* OC_NIC_DRV_INTERNAL_H */
