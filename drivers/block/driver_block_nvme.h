/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07 / WP-10a
 * File: kernel/nvme.h
 * Purpose: NVMe PCI driver - public init entry point + WP-10a additions.
 *
 * Probes for an NVMe controller (PCI class 0x010802).  If found, brings
 * the controller online, creates the admin queue pair plus multiple I/O
 * queue pairs (WP-10a: 2, both fully working), performs Identify
 * Namespace to read capacity/sector size, and registers a single block
 * device ("nvme0") with the blk layer.
 *
 * WP-10a additions:
 *   driver_block_nvme_print_state()  - controller/queue report for the `nvme` command
 *   driver_block_nvme_num_io_queues() - how many I/O queue pairs are live
 *   driver_block_nvme_blk_index()     - blk layer index of the nvme0 device
 *   device Flush (opcode 0x0A) wired into the driver_block_flush() path
 */
#ifndef OC_NVME_H
#define OC_NVME_H

#include "driver_pci.h"

/* WP-10a L1 extension interface: initialize an NVMe controller.
 *
 * pdev != NULL : bring up exactly that PCI function (class 0x010802).
 * pdev == NULL : probe the PCI bus and initialize the first NVMe
 *                controller found (the kmain boot path).
 *
 * Returns 0 when a controller was brought online, 1 when no controller
 * was found (pdev == NULL only), -1 on error or a non-NVMe pdev. */
int driver_block_nvme_init(driver_pci_dev_t *pdev);

/* WP-10a: print controller + I/O queue state (for the `nvme` command). */
void driver_block_nvme_print_state(void);

/* WP-10a: number of live I/O queue pairs (>= 1 when a device exists). */
int driver_block_nvme_num_io_queues(void);

/* WP-10a: blk-layer device index of "nvme0", or -1 if absent. */
int driver_block_nvme_blk_index(void);

#endif /* OC_NVME_H */
