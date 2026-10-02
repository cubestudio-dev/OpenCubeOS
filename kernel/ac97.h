/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/ac97.h
 * Purpose: Intel 82801AA/AB AC'97 codec driver interface (ac97.c).
 *
 * L1 extension surface:
 *
 *   int ac97_init(pci_dev_t *pdev);
 *       Initialise the first AC'97 controller (8086:2415/2445 or class
 *       0x040100).  On success a SND_TYPE_AC97 device is registered
 *       with the snd framework; -1 when no controller is present or
 *       init fails.
 *
 *   void ac97_print_state(void);
 *       One-line controller status for the `ac97` shell command.
 */
#ifndef OC_AC97_H
#define OC_AC97_H

#include "types.h"
#include "pci.h"

int  ac97_init(pci_dev_t *pdev);
void ac97_print_state(void);

#endif /* OC_AC97_H */
