/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_ath.h
 *
 * Atheros/Qualcomm 802.11 family driver (ath9k / ath10k / ath11k) -
 * public interface.  Registered with the 802.11 core at boot.
 */
#ifndef DRIVER_ATH_H
#define DRIVER_ATH_H

/* Register all three Atheros sub-families with the wireless core. */
void driver_ath_register(void);

#endif /* DRIVER_ATH_H */
