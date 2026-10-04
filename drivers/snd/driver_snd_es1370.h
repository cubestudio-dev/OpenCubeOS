/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/es1370.h
 * Purpose: Ensoniq AudioPCI ES1370/ES1371 driver interface (es1370.c).
 *
 * L1 extension surface:
 *
 *   int driver_snd_es1370_init(driver_pci_dev_t *pdev);
 *       Initialise the first ES1370/ES1371 (1274:5000/1371 or class
 *       0x040100).  On success a SND_TYPE_ES1370 device is registered
 *       with the snd framework; -1 when no controller is present or
 *       init fails.
 *
 *   void driver_snd_es1370_print_state(void);
 *       One-line controller status for the `es1370` shell command.
 */
#ifndef OC_ES1370_H
#define OC_ES1370_H

#include "types.h"
#include "driver_pci.h"

int  driver_snd_es1370_init(driver_pci_dev_t *pdev);
void driver_snd_es1370_print_state(void);

#endif /* OC_ES1370_H */
