/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10a
 * File: kernel/disk_test_cmds.h
 * Purpose: WP-10a storage-driver test commands.
 *
 * Registers the WP-10a test suite as shell commands:
 *   driver_block_ahci_test        AHCI controller init, port discovery, read/write
 *   driver_block_nvme_test        NVMe controller init, queues, read/write
 *   driver_block_ata_dma_test     ATA Bus-Master DMA read/write + PIO speed comparison
 *   driver_block_virtio_blk_test  virtio-blk read/write + capacity check
 *   disk_rw_test     read/write round-trip on one device (arg = dev name)
 *   partition_test   MBR + GPT partition parsing on one device (fixture)
 *   fs_mount_test    format + mount FAT32 + file round-trip (arg = dev)
 *   real_hw_test     real-hardware detection report (NOT RUN in a VM)
 *
 * Every test prints, per check: input / expected / actual / PASS|FAIL,
 * and every destructive step (pattern writes) backs up and restores the
 * sectors it touched.  Tests that need a device take the device name as
 * an argument - nothing is formatted or overwritten implicitly.
 */
#ifndef OC_DISK_TEST_CMDS_H
#define OC_DISK_TEST_CMDS_H

void shell_cmds_disk_test_register(void);

#endif /* OC_DISK_TEST_CMDS_H */
