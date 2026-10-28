/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_brcm.h
 *
 * Broadcom 802.11 family driver (b43 / brcmfmac, PCI + SDIO) - public
 * interface.  Registered with the 802.11 core + the SDIO bus at boot.
 */
#ifndef DRIVER_BRCM_H
#define DRIVER_BRCM_H

/* Register the Broadcom families with the wireless core and the SDIO
 * client-driver registry. */
void driver_brcm_register(void);

#endif /* DRIVER_BRCM_H */
