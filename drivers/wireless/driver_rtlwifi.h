/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_rtlwifi.h
 *
 * Realtek 802.11 family driver (rtl8188/rtl8192/rtl8821, PCIe + USB) -
 * public interface.  Registered with the 802.11 core at boot.
 */
#ifndef DRIVER_RTLWIFI_H
#define DRIVER_RTLWIFI_H

/* Register the Realtek family with the wireless core. */
void driver_rtlwifi_register(void);

#endif /* DRIVER_RTLWIFI_H */
