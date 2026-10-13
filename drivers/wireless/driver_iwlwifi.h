/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_iwlwifi.h
 *
 * Intel Wireless (iwlwifi-class) family driver - public interface.
 * Registered with the 802.11 core (driver_wireless.h) at boot.
 */
#ifndef DRIVER_IWLWIFI_H
#define DRIVER_IWLWIFI_H

/* Register the iwlwifi family with the wireless core.  Called once from
 * the wireless init block in kernel/main.c. */
void driver_iwlwifi_register(void);

#endif /* DRIVER_IWLWIFI_H */
