/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/blk.h
 * Purpose: Block device abstraction layer.
 *
 * WP-07 adds a unified block-device API so that ATA, virtio-blk, NVMe,
 * and any future block device all expose the same read/write-sectors
 * interface.  File systems (FAT32, exFAT, ext4) and the partition parser
 * talk to block devices through this layer, never directly to a driver.
 *
 * L1 extension interface (item 27): blk_register_device / blk_unregister_device
 * L1 extension interface (item 28): blk_read_sectors / blk_write_sectors
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
} blk_type_t;

/* Forward declaration. */
typedef struct blk_device blk_device_t;

/* Driver operations — each block device fills these in. */
typedef struct blk_ops {
    /* Read `count` sectors starting at `lba` into `buf`. Returns 0 on
     * success, negative on error. `buf` must be count*512 bytes. */
    int (*read)(blk_device_t *dev, u64 lba, u32 count, void *buf);
    /* Write `count` sectors from `buf` starting at `lba`. */
    int (*write)(blk_device_t *dev, u64 lba, u32 count, const void *buf);
} blk_ops_t;

struct blk_device {
    char     name[BLK_DEV_NAME_LEN];   /* e.g. "hda", "vda", "nvme0" */
    blk_type_t type;
    u64      sectors;                  /* total sectors (capacity) */
    u32      sector_size;              /* almost always 512 */
    u8       present;                  /* 1 = usable */
    u8       bus, dev, func;           /* PCI address (for virtio/nvme) */
    const blk_ops_t *ops;              /* driver read/write */
    void    *priv;                     /* driver private data */
};

/* Statistics for a block device. */
typedef struct {
    u64 reads;
    u64 writes;
    u64 read_sectors;
    u64 write_sectors;
} blk_stats_t;

/* ---- L1 extension API ---- */

/* Initialize the block-device subsystem. */
void blk_init(void);

/* Register a block device. Returns the device index (>=0) or -1 on full. */
int blk_register_device(const char *name, blk_type_t type,
                        u64 sectors, u32 sector_size,
                        const blk_ops_t *ops, void *priv);

/* Unregister a block device by index. */
int blk_unregister_device(int index);

/* Look up a device by name (e.g. "hda"). Returns index or -1. */
int blk_find_device(const char *name);

/* Get a device by index. Returns NULL if out of range. */
blk_device_t *blk_get_device(int index);

/* Read/write sectors through a device index. These go through the disk
 * cache (write-back). Returns 0 on success. */
int blk_read_sectors(int dev_idx, u64 lba, u32 count, void *buf);
int blk_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf);

/* Direct (uncached) read/write — used by the cache itself and by tools
 * that must bypass caching (e.g. mkfs). */
int blk_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf);
int blk_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf);

/* List all block devices (for `lsblk` command). */
void blk_list_devices(void);

/* Get stats for a device. */
void blk_get_stats(int dev_idx, blk_stats_t *out);

/* Number of registered devices. */
int blk_num_devices(void);

#endif /* OC_BLK_H */
