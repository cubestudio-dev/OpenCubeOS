/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/block: driver-internal interface.
 *
 * Kernel-internal ONLY: block drivers (the block driver files, USB MSC,
 * SCSI targets, the partition mapper and the g8 test ramdisk) include
 * this header to add/remove probed devices on the block service
 * table.  NOT part of the L1 surface: the register-class entries were
 * removed in v2.0.0; drivers declare identity with
 * OC_DRIVER_DECL("block", ...) and mutate the table only from inside
 * their own probe/detach paths.
 */
#ifndef OC_BLK_DRV_INTERNAL_H
#define OC_BLK_DRV_INTERNAL_H

#include "block_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probe descriptor each block driver declares in its own file:
 *   static const blk_drv_ops_t g_probe = { my_probe };
 *   OC_DRIVER_DECL("block", "ata", &g_probe, "ATA/ATAPI (PIO)");
 * my_probe() scans the bus and adds devices via blk_dev_add(). */
typedef struct {
    int (*probe)(void);   /* 0 = at least one device was added */
} blk_drv_ops_t;

/* Register a block device (was the L1-surface
 * driver_block_register_device(); internal since v2.0.0).
 * Returns the device index (>=0) or -1 on full. */
int blk_dev_add(const char *name, driver_block_type_t type,
                u64 sectors, u32 sector_size,
                const driver_block_ops_t *ops, void *priv);

/* Pointer-form add for prefilled devices (was
 * driver_block_register(); internal since v2.0.0). */
int blk_dev_add_ptr(driver_block_device_t *dev, const driver_block_ops_t *ops);

/* Unregister a block device by index (was
 * driver_block_unregister_device(); internal since v2.0.0). */
int blk_dev_remove(int index);

/* Replace the ops of an already-added device (was
 * driver_block_set_ops(); internal since v2.0.0).  Used by the ATA
 * driver to upgrade a PIO drive to Bus-Master DMA. */
int blk_dev_set_ops(int dev_idx, const driver_block_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* OC_BLK_DRV_INTERNAL_H */
