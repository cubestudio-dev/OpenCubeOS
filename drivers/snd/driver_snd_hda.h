/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/hda.h
 * Purpose: Intel HD Audio driver interface (implementation: hda.c).
 *
 * L1 extension surface:
 *
 *   int driver_snd_hda_init(driver_pci_dev_t *pdev);
 *       Initialise the first Intel HDA controller.  pdev == NULL scans
 *       the PCI bus for 8086:2668/293E/293F/3A3E or class 0x040300;
 *       pdev != NULL initialises exactly that PCI function.  On success
 *       the driver registers a SND_TYPE_HDA device with the snd
 *       framework and returns 0; -1 when no controller is present or
 *       the reset/codec-enumeration path fails.
 *
 *   void driver_snd_hda_print_state(void);
 *       One-line controller status for the `hda` shell command.
 */
#ifndef OC_HDA_H
#define OC_HDA_H

#include "types.h"
#include "driver_pci.h"

int  driver_snd_hda_init(driver_pci_dev_t *pdev);
void driver_snd_hda_print_state(void);

#endif /* OC_HDA_H */
