/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07 / WP-10a
 * File: drivers/block/driver_block_blk.h
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
/* v2.0.0: register-class entry points (driver_block_register,
 * driver_block_register_device, driver_block_unregister_device,
 * driver_block_set_ops) were REMOVED from the L1 surface - see
 * drv_internal.h for the kernel-internal replacements. */
#ifndef OC_BLK_H
#define OC_BLK_H

#include "drv_storage_api.h"


/* ---- L1 extension API ---- */

/* Initialize the block-device subsystem. */
void block_svc_init(void);
int  block_svc_probe_all(void);

/* v2.0.0: register-class entries REMOVED from the L1 surface.
 * Block drivers declare identity with OC_DRIVER_DECL("block", ...) and
 * add probed devices via blk_dev_add()/blk_dev_remove()
 * (services/block/drv_internal.h, kernel-internal). */


/* Read/write sectors through a device index. These go through the disk
 * cache (write-back). Returns 0 on success. */
int driver_block_read_sectors(int dev_idx, u64 lba, u32 count, void *buf);
int driver_block_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf);

/* Direct (uncached) read/write — used by the cache itself and by tools
 * that must bypass caching (e.g. mkfs). */
int driver_block_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf);
int driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf);

/* ---- 3.0.0-WP03 G4: whole-disk hard protection ----------------------
 *
 * When the OCIRE embed is active (the recovery supervisor handed the
 * machine over with a mailbox), the disk carrying the mailbox/log is
 * the RECOVERY DISK: every write through a covering device index is
 * REFUSED (the whole-disk raw-write surface, e.g. disk_rw_test hda,
 * is closed).  Reads stay open.  The OCOS partition on a normal
 * dual-boot layout never exists as a child device (the 0xEE GPT
 * shadow makes partscan see zero partitions - WP-01 isolation), so
 * the guard cannot break a mounted filesystem: there is none on
 * that disk.  Partition-child writes on OTHER disks go through the
 * trusted path below (kernel-internal, parent ops direct).
 *
 * driver_block_write_sectors_trusted: kernel-internal channel for
 * the OCIRE embed service itself (mailbox signal + boot-log ring
 * appends) and for partition children writing through their parent.
 * Never callable from ring 3. */
void driver_block_set_protected(int dev_idx);
int  driver_block_is_protected(int dev_idx);
int  driver_block_read_sectors_trusted(int dev_idx, u64 lba, u32 count, void *buf);
int  driver_block_write_sectors_trusted(int dev_idx, u64 lba, u32 count, const void *buf);

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
int driver_block_read (driver_block_device_t *dev, u64 lba, u32 count, void *buf);
int driver_block_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
int driver_block_flush(driver_block_device_t *dev);


/* List all block devices (for `lsblk` command). */
void driver_block_list_devices(void);

/* Get stats for a device. */
void driver_block_get_stats(int dev_idx, driver_block_stats_t *out);



#endif /* OC_BLK_H */
