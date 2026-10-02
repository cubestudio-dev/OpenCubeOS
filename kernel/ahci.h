/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10a
 * File: kernel/ahci.h
 * Purpose: AHCI (SATA) host controller driver interface.
 *
 * WP-10a adds an AHCI driver so Open Cube OS can read/write mainstream
 * SATA disks and SSDs (class code 0x010601).  The driver:
 *   - enumerates AHCI PCI controllers (pci_find_class_exact),
 *   - programs the controller (GHC.AE) and every implemented port,
 *   - builds command lists / command tables / H2D FIS in host memory,
 *   - performs polled 48-bit LBA DMA reads/writes (READ/WRITE DMA EXT)
 *     through a per-port bounce buffer,
 *   - registers every detected ATA drive with the blk layer ("sda"...),
 *   - exposes a FLUSH (FLUSH CACHE EXT) hook for blk_flush().
 *
 * Polling model: no interrupts (matches the WP-05 ATA PIO and WP-07 NVMe
 * drivers).  Hot-plug notification is intentionally not implemented
 * (PxIE stays 0); presence is detected at init time via PxSSTS.DET.
 */
#ifndef OC_AHCI_H
#define OC_AHCI_H

#include "types.h"
#include "pci.h"

/* Maximum SATA drives we register with the blk layer. */
#define AHCI_MAX_DRIVES 4

/* WP-10a L1 extension interface: initialize an AHCI controller.
 *
 * pdev != NULL : bring up exactly that PCI function (class 0x010601),
 *                program its ports and register attached SATA drives.
 * pdev == NULL : enumerate the PCI bus and initialize every AHCI
 *                controller found (the kmain boot path).
 *
 * Returns the number of controllers initialized (>= 0), or -1 when an
 * explicit pdev was requested but is not an AHCI controller. */
int ahci_init(pci_dev_t *pdev);

/* Number of SATA drives registered by the driver (0 if none). */
int ahci_num_drives(void);

/* Index of the blk-layer device for SATA drive `n`, or -1.
 * (Convenience for the ahci_test command.) */
int ahci_blk_index(int n);

/* Human-readable controller state, printed by the `ahci` shell command:
 * PCI BDF, BAR5, CAP, GHC, PI (implemented-port mask), per-port SSTS/TFD. */
void ahci_print_state(void);

#endif /* OC_AHCI_H */
