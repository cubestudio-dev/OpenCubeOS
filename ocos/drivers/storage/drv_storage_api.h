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

#include "types.h"

#define BLK_MAX_DEVICES   8
#define BLK_SECTOR_SIZE   512
#define BLK_DEV_NAME_LEN  32

/* Block-device type. */
typedef enum {
    BLK_TYPE_ATA    = 1,
    BLK_TYPE_VIRTIO = 2,
    BLK_TYPE_NVME   = 3,
    BLK_TYPE_AHCI   = 4,
    BLK_TYPE_USB    = 5,   /* WP-10d: USB Mass Storage (BOT + SCSI) */
    BLK_TYPE_SCSI   = 6,   /* WP-10-camera_misc: LSI 53C895A HBA    */
} driver_block_type_t;

/* Forward declaration. */
typedef struct driver_block_device driver_block_device_t;

/* Driver operations — each block device fills these in.  All three
 * hooks return 0 on success, negative on error. */
typedef struct driver_block_ops {
    /* Read `count` sectors starting at `lba` into `buf`. Returns 0 on
     * success, negative on error. `buf` must be count*512 bytes. */
    int (*read)(driver_block_device_t *dev, u64 lba, u32 count, void *buf);
    /* Write `count` sectors from `buf` starting at `lba`. */
    int (*write)(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
    /* WP-10a: flush the drive's internal write cache to media (ATA
     * FLUSH CACHE, NVMe Flush, virtio-blk FLUSH, ...).  Optional —
     * drivers without a flush command leave it NULL; driver_block_flush() then
     * only flushes the OS-level write-back cache. */
    int (*flush)(driver_block_device_t *dev);
} driver_block_ops_t;

struct driver_block_device {
    char     name[BLK_DEV_NAME_LEN];   /* e.g. "sda", "hda", "vda", "nvme0" */
    driver_block_type_t type;
    u64      sectors;                  /* total sectors (capacity) */
    u32      sector_size;              /* almost always 512 */
    u8       present;                  /* 1 = usable */
    u8       bus, dev, func;           /* PCI address (for virtio/nvme) */
    const driver_block_ops_t *ops;              /* driver read/write/flush */
    void    *priv;                     /* driver private data */
    int      index;                    /* WP-10a: registry slot filled in
                                         by driver_block_register() */
};

/* Statistics for a block device. */
typedef struct {
    u64 reads;
    u64 writes;
    u64 read_sectors;
    u64 write_sectors;
} driver_block_stats_t;


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


/* ---- Registry self-lookup (driver interface; implemented by the
 * block service) ----
 * A driver looks up its own registered device (e.g. to upgrade the
 * ops of a PIO drive to Bus-Master DMA) through these. */
int driver_block_find_device(const char *name);   /* index / -1 */
driver_block_device_t *driver_block_get_device(int index);
int driver_block_num_devices(void);

#endif /* OC_BLK_DRV_INTERNAL_H */
