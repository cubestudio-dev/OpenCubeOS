/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07 / WP-10a
 * File: kernel/blk.h
 * Purpose: Block device abstraction layer.
 *
 * WP-07 adds a unified block-device API so that ATA, virtio-blk, NVMe,
 * and any future block device all expose the same read/write-sectors
 * interface.  File systems (FAT32, exFAT, ext4) and the partition parser
 * talk to block devices through this layer, never directly to a driver.
 *
 * L1 extension interface (item 27): driver_block_register_device / driver_block_unregister_device
 * L1 extension interface (item 28): driver_block_read_sectors / driver_block_write_sectors
 *
 * WP-10a adds the driver-facing registration/IO API used by the new
 * storage drivers (AHCI, NVMe, ATA Bus-Master DMA) and by external L1
 * extensions that bring their own block hardware:
 *   - driver_block_ops_t gains an optional .flush hook (FLUSH CACHE / NVMe Flush /
 *     virtio-blk FLUSH) so write-back cache data can be pushed to media;
 *   - driver_block_register(dev, ops)   register a fully-filled driver_block_device_t;
 *   - driver_block_read(dev, ...)       cached read through a device pointer;
 *   - driver_block_write(dev, ...)      cached (write-back) write via pointer;
 *   - driver_block_flush(dev)           write-back dirty sectors + driver flush.
 */
#ifndef OC_BLK_H
#define OC_BLK_H

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

/* ---- L1 extension API ---- */

/* Initialize the block-device subsystem. */
void driver_block_init(void);

/* Register a block device. Returns the device index (>=0) or -1 on full. */
int driver_block_register_device(const char *name, driver_block_type_t type,
                        u64 sectors, u32 sector_size,
                        const driver_block_ops_t *ops, void *priv);

/* Unregister a block device by index. */
int driver_block_unregister_device(int index);

/* Look up a device by name (e.g. "hda"). Returns index or -1. */
int driver_block_find_device(const char *name);

/* Get a device by index. Returns NULL if out of range. */
driver_block_device_t *driver_block_get_device(int index);

/* Read/write sectors through a device index. These go through the disk
 * cache (write-back). Returns 0 on success. */
int driver_block_read_sectors(int dev_idx, u64 lba, u32 count, void *buf);
int driver_block_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf);

/* Direct (uncached) read/write — used by the cache itself and by tools
 * that must bypass caching (e.g. mkfs). */
int driver_block_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf);
int driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf);

/* ---- WP-10a: pointer-based driver API (L1 extension interface) ----
 *
 * These four functions are the recommended way for a driver or an L1
 * extension to bring up a new block device.
 *
 * driver_block_register:
 *   Fill a driver_block_device_t (name/type/sectors/sector_size/priv — present
 *   is ignored) and pass it together with the driver's driver_block_ops_t.
 *   The registry copies the device into its table, assigns the slot
 *   number to dev->index and returns it (>= 0), or -1 if the table is
 *   full or arguments are invalid.
 *
 * driver_block_read / driver_block_write:
 *   Cached sector I/O through the device pointer (same semantics as
 *   driver_block_read_sectors/driver_block_write_sectors, which take an index).
 *
 * driver_block_flush:
 *   Push the OS write-back cache for this device to the driver, then
 *   invoke the driver's .flush hook (if any).  Call before assuming
 *   data is on media (e.g. before shutdown or fsync semantics).
 */
int driver_block_register(driver_block_device_t *dev, const driver_block_ops_t *ops);
int driver_block_read (driver_block_device_t *dev, u64 lba, u32 count, void *buf);
int driver_block_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
int driver_block_flush(driver_block_device_t *dev);

/* WP-10a: replace the driver ops of an already-registered device (used
 * by the ATA driver to upgrade a PIO-registered drive to Bus-Master DMA
 * after driver_block_ata_dma_init() validated the controller).  Returns 0 on success. */
int driver_block_set_ops(int dev_idx, const driver_block_ops_t *ops);

/* List all block devices (for `lsblk` command). */
void driver_block_list_devices(void);

/* Get stats for a device. */
void driver_block_get_stats(int dev_idx, driver_block_stats_t *out);

/* Number of registered devices. */
int driver_block_num_devices(void);

#endif /* OC_BLK_H */
