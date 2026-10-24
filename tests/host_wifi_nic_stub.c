/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - host link stub for tests/host_wifi_test.c.
 *
 * WP-10-tui: the wireless core carries a wl_nic bridge
 * (wl_nic_bridge_register) that adds its NIC through the v2.0.0
 * file-declaration path (nic_dev_add, services/nic/nic_svc.c).  The
 * host wifi harness exercises the 802.11 core only, so instead of
 * linking the whole NIC family tree we provide a recording stub:
 * the bridge never registers hardware in a host run (no probe), and
 * the stub keeps that honest by returning "added" like the real
 * framework would for an empty table entry. */
#include "../../services/nic/nic_svc.h"

int nic_dev_add(driver_nic_device_t *dev, const driver_nic_ops_t *ops) {
    (void)dev;
    (void)ops;
    return 0;
}
