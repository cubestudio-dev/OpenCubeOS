/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07 / WP-10a
 * File: drivers/block/driver_block_blk.c
 * Purpose: Block device registry + cached read/write dispatch.
 *
 * Every block device (ATA, virtio-blk, NVMe, AHCI SATA) registers here.
 * All reads and writes go through the disk cache (driver_block_cache.c) so file
 * systems do not need to know about caching.  WP-10a adds the
 * pointer-based driver API (driver_block_register/driver_block_read/driver_block_write/driver_block_flush)
 * with an optional per-driver flush hook.
 */
#include "block_svc.h"
#include "blk_drv_internal.h"
#include "core_registry.h"
#include "driver_block_cache.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"

static driver_block_device_t g_blk_devs[BLK_MAX_DEVICES];
static driver_block_stats_t  g_blk_stats[BLK_MAX_DEVICES];
static int          g_blk_count = 0;

void block_svc_init(void) {
    memset(g_blk_devs, 0, sizeof(g_blk_devs));
    memset(g_blk_stats, 0, sizeof(g_blk_stats));
    g_blk_count = 0;
}

/* BUG-0040+0054 FIX (A6-1/A6-15): the block and cache layers are
 * hardwired to 512-byte sectors (BLK_SECTOR_SIZE, cache slot data[512],
 * driver_block_read_sectors steps i*BLK_SECTOR_SIZE).  A device whose
 * formatted sector size is anything else (NVMe 4Kn LBADS, USB MSC
 * 2048/4096-byte media, ...) made every cached read/write overrun its
 * 512-byte caller buffers by sector_size-512 bytes per sector (3584 B
 * on 4Kn).  The NVMe driver already refuses non-512 LBADS fail-closed
 * (BUG-0040 precedent in driver_block_nvme.c); this gate extends the
 * same refusal to BOTH block-layer registration entry points so no
 * driver can register a device the cache cannot serve safely.
 * sector_size == 0 used to silently default to 512; a device that
 * cannot even report a sector size is now rejected too - an honest
 * failure beats a silent guess.  Returns 0 (size OK) or -1 (rejected,
 * honest log line printed). */
static int driver_block_check_sector_size(const char *name, u32 sector_size) {
    if (sector_size == BLK_SECTOR_SIZE) return 0;
    char line[160]; char n[24];
    strcpy(line, "blk: refusing device '");
    if (name) strcat(line, name);
    strcat(line, "': only 512-byte sectors supported (got ");
    u64_to_str((u64)sector_size, n);
    strcat(line, n);
    strcat(line, ") - device NOT registered\n");
    screen_console_puts(line);
    return -1;
}

int blk_dev_add(const char *name, driver_block_type_t type,
                        u64 sectors, u32 sector_size,
                        const driver_block_ops_t *ops, void *priv) {
    /* BUG-0040+0054: fail-closed sector-size gate (see above). */
    if (driver_block_check_sector_size(name, sector_size) != 0) return -1;
    if (g_blk_count >= BLK_MAX_DEVICES) return -1;
    int idx = -1;
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (!g_blk_devs[i].present) { idx = i; break; }
    }
    if (idx < 0) return -1;
    driver_block_device_t *d = &g_blk_devs[idx];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, name, BLK_DEV_NAME_LEN - 1);
    d->name[BLK_DEV_NAME_LEN - 1] = 0;
    d->type = type;
    d->sectors = sectors;
    d->sector_size = sector_size;   /* gate above guarantees 512 */
    d->present = 1;
    d->ops = ops;
    d->priv = priv;
    d->index = idx;
    if (idx >= g_blk_count) g_blk_count = idx + 1;
    return idx;
}

int blk_dev_remove(int index) {
    if (index < 0 || index >= BLK_MAX_DEVICES) return -1;
    driver_block_device_t *d = &g_blk_devs[index];
    if (!d->present) return -1;
    d->present = 0;
    /* BUG-0313 (A6-17): unregister used to only clear `present`, leaving
     * name/ops/priv and the per-slot stats behind, and never shrank
     * g_blk_count - once all 8 slots had been used, a later unregister +
     * register failed forever with -ENOSPC even though a slot was free.
     * Clear the stale identity, reset the per-slot stats, and shrink the
     * high-water mark while the top slots are all free. */
    d->name[0] = 0;
    d->ops = NULL;
    d->priv = NULL;
    memset(&g_blk_stats[index], 0, sizeof(g_blk_stats[index]));
    while (g_blk_count > 0 && !g_blk_devs[g_blk_count - 1].present)
        g_blk_count--;
    return 0;
}

int driver_block_find_device(const char *name) {
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (g_blk_devs[i].present && strncmp(g_blk_devs[i].name, name, BLK_DEV_NAME_LEN) == 0)
            return i;
    }
    return -1;
}

driver_block_device_t *driver_block_get_device(int index) {
    if (index < 0 || index >= BLK_MAX_DEVICES) return NULL;
    if (!g_blk_devs[index].present) return NULL;
    return &g_blk_devs[index];
}

/* ---- 3.0.0-WP03 G4: whole-disk hard protection ------------------- */

static int g_blk_protected[BLK_MAX_DEVICES];

void driver_block_set_protected(int dev_idx) {
    if (dev_idx >= 0 && dev_idx < BLK_MAX_DEVICES) g_blk_protected[dev_idx] = 1;
}

int driver_block_is_protected(int dev_idx) {
    return (dev_idx >= 0 && dev_idx < BLK_MAX_DEVICES)
             ? g_blk_protected[dev_idx] : 0;
}

/* Kernel-internal trusted channel (the OCIRE embed service: mailbox
 * boot signal + boot-log ring appends).  Identical to the raw path
 * but never guarded - not reachable from ring 3. */
int driver_block_read_sectors_trusted(int dev_idx, u64 lba, u32 count, void *buf) {
    return driver_block_read_sectors_raw(dev_idx, lba, count, buf);
}

int driver_block_write_sectors_trusted(int dev_idx, u64 lba, u32 count, const void *buf) {
    driver_block_device_t *d = driver_block_get_device(dev_idx);
    if (!d || !d->ops || !d->ops->write) return -1;
    int rc = d->ops->write(d, lba, count, buf);
    if (rc == 0) {
        g_blk_stats[dev_idx].writes++;
        g_blk_stats[dev_idx].write_sectors += count;
    }
    return rc;
}

int driver_block_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf) {
    driver_block_device_t *d = driver_block_get_device(dev_idx);
    if (!d || !d->ops || !d->ops->read) return -1;
    int rc = d->ops->read(d, lba, count, buf);
    if (rc == 0) {
        g_blk_stats[dev_idx].reads++;
        g_blk_stats[dev_idx].read_sectors += count;
    }
    return rc;
}

int driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf) {
    driver_block_device_t *d = driver_block_get_device(dev_idx);
    if (!d || !d->ops || !d->ops->write) return -1;
    /* 3.0.0-WP03 G4: the recovery disk is write-closed.  Refuse BEFORE
     * touching the device (whole-disk hard protection; reads stay
     * open).  The embed service writes through the trusted channel. */
    if (g_blk_protected[dev_idx]) {
        char line[96]; char n[24];
        strcpy(line, "blk: write to protected device ");
        if (d->name[0]) strcat(line, d->name);
        u64_to_str(lba, n); strcat(line, " lba="); strcat(line, n);
        strcat(line, " REFUSED (ocire recovery disk)\n");
        screen_console_puts(line);
        return -1;
    }
    int rc = d->ops->write(d, lba, count, buf);
    if (rc == 0) {
        g_blk_stats[dev_idx].writes++;
        g_blk_stats[dev_idx].write_sectors += count;
    }
    return rc;
}

/* Cached read: try the cache first, fall back to raw read. */
int driver_block_read_sectors(int dev_idx, u64 lba, u32 count, void *buf) {
    /* For simplicity and correctness, we cache at the single-sector level.
     * File systems typically read one or a few sectors at a time. */
    u8 *out = (u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        if (driver_block_cache_read(dev_idx, lba + i, out + (u64)i * BLK_SECTOR_SIZE) != 0) {
            /* Cache miss: read from device. */
            if (driver_block_read_sectors_raw(dev_idx, lba + i, 1, out + (u64)i * BLK_SECTOR_SIZE) != 0)
                return -1;
            driver_block_cache_insert(dev_idx, lba + i, out + (u64)i * BLK_SECTOR_SIZE);
        }
    }
    return 0;
}

/* Cached write: update cache, mark dirty (write-back).
 * BUG-0116 FIX: this path used to return 0 unconditionally, so a device
 * that failed its (eviction or flush) write-back silently dropped data
 * while every caller believed the write succeeded. Errors from the
 * cache layer (including the write-through fallback) now propagate. */
int driver_block_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf) {
    /* 3.0.0-WP03 G4: refuse BEFORE creating dirty cache lines on the
     * protected recovery disk (the raw-write guard alone would leave
     * unflushable dirty slots behind). */
    if (dev_idx >= 0 && dev_idx < BLK_MAX_DEVICES && g_blk_protected[dev_idx]) {
        screen_console_puts("blk: cached write to protected device REFUSED (ocire recovery disk)\n");
        return -1;
    }
    const u8 *src = (const u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        if (driver_block_cache_write(dev_idx, lba + i, src + (u64)i * BLK_SECTOR_SIZE) != 0)
            return -1;
    }
    return 0;
}

/* ---- WP-10a: pointer-based driver API ---- */

int blk_dev_add_ptr(driver_block_device_t *dev, const driver_block_ops_t *ops) {
    if (!dev || !ops || !ops->read || !ops->write) return -1;
    if (!dev->name[0]) return -1;
    /* BUG-0040+0054: fail-closed sector-size gate - the pointer-based
     * API is the second registration entry point (USB MSC, partitions,
     * L1 extensions) and must enforce the same 512-byte contract. */
    if (driver_block_check_sector_size(dev->name, dev->sector_size) != 0) return -1;
    if (g_blk_count >= BLK_MAX_DEVICES) return -1;
    int idx = -1;
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (!g_blk_devs[i].present) { idx = i; break; }
    }
    if (idx < 0) return -1;
    driver_block_device_t *d = &g_blk_devs[idx];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, dev->name, BLK_DEV_NAME_LEN - 1);
    d->name[BLK_DEV_NAME_LEN - 1] = 0;
    d->type = dev->type;
    d->sectors = dev->sectors;
    d->sector_size = dev->sector_size;   /* gate above guarantees 512 */
    d->present = 1;
    d->ops = ops;
    d->priv = dev->priv;
    d->bus = dev->bus; d->dev = dev->dev; d->func = dev->func;
    d->index = idx;
    if (dev) dev->index = idx;   /* report slot back to the caller */
    if (idx >= g_blk_count) g_blk_count = idx + 1;
    return idx;
}

int driver_block_read(driver_block_device_t *dev, u64 lba, u32 count, void *buf) {
    if (!dev || dev->index < 0 || dev->index >= BLK_MAX_DEVICES) return -1;
    return driver_block_read_sectors(dev->index, lba, count, buf);
}

int driver_block_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf) {
    if (!dev || dev->index < 0 || dev->index >= BLK_MAX_DEVICES) return -1;
    return driver_block_write_sectors(dev->index, lba, count, buf);
}

/* WP-10a: replace the driver ops of an already-registered device. */
int blk_dev_set_ops(int dev_idx, const driver_block_ops_t *ops) {
    driver_block_device_t *d = driver_block_get_device(dev_idx);
    if (!d || !ops || !ops->read || !ops->write) return -1;
    d->ops = ops;
    return 0;
}

/* Flush the OS write-back cache for this device, then ask the driver to
 * flush its own on-media cache.  A driver without a flush hook only
 * gets the OS-level flush (still correct, just weaker durability). */
int driver_block_flush(driver_block_device_t *dev) {
    if (!dev || dev->index < 0 || dev->index >= BLK_MAX_DEVICES) return -1;
    int idx = dev->index;
    if (driver_block_cache_flush_dev(idx) != 0) return -1;
    if (dev->ops && dev->ops->flush) return dev->ops->flush(dev);
    return 0;
}

void driver_block_list_devices(void) {
    char line[120]; char n[24];
    screen_console_puts("Block devices:\n");
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (!g_blk_devs[i].present) continue;
        driver_block_device_t *d = &g_blk_devs[i];
        const char *tn = "???";
        switch (d->type) {
            case BLK_TYPE_ATA:    tn = "ATA";    break;
            case BLK_TYPE_VIRTIO: tn = "virtio"; break;
            case BLK_TYPE_NVME:   tn = "NVMe";   break;
            case BLK_TYPE_AHCI:   tn = "AHCI";   break;
            case BLK_TYPE_USB:    tn = "usb";    break;
            case BLK_TYPE_SCSI:   tn = "scsi";   break;
        }
        strcpy(line, "  "); strcat(line, d->name);
        strcat(line, " ["); strcat(line, tn); strcat(line, "] ");
        u64_to_str(d->sectors, n); strcat(line, n);
        strcat(line, " sectors (");
        u64_to_str(d->sectors / 2, n); strcat(line, n);
        strcat(line, " KiB) sector_size=");
        u64_to_str(d->sector_size, n); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }
}

void driver_block_get_stats(int dev_idx, driver_block_stats_t *out) {
    if (dev_idx < 0 || dev_idx >= BLK_MAX_DEVICES || !out) return;
    *out = g_blk_stats[dev_idx];
}

int driver_block_num_devices(void) {
    int n = 0;
    for (int i = 0; i < BLK_MAX_DEVICES; i++)
        if (g_blk_devs[i].present) n++;
    return n;
}

/* ---- v2.0.0 file-local service identity ---- */
/* v2.0.0: the service identity (with its ring-3 surface) is declared
 * in services/block/block_l1.c - one declaration per file that owns
 * it; the bring-up callbacks stay here. */

/* v2.0.0: the block-driver probe list is COLLECTED from the
 * OC_DRIVER_DECL("block", ...) declarations (was hardcoded kmain
 * calls).  Order below preserves the historical bring-up: Bus-Master
 * DMA first (it may register a drive with DMA ops), then the PIO
 * registration pass (skips drives the DMA path already took), then
 * the modern buses.  Families absent from this build are simply
 * missing from the registry. */
int block_svc_probe_all(void) {
    static const char *const order[] = {
        "ata_dma", "ata", "virtio_blk", "ahci", "nvme", "scsi",
    };
    int found = 0;
    for (unsigned k = 0; k < sizeof(order) / sizeof(order[0]); k++) {
        for (int i = 0; i < core_registry_drvs_count(); i++) {
            const oc_driver_decl_t *c = core_registry_drv_at(i);
            if (!c || strcmp(c->family, "block") != 0) continue;
            if (strcmp(c->name, order[k]) != 0) continue;
            const blk_drv_ops_t *ops = (const blk_drv_ops_t *)c->ops;
            if (ops && ops->probe && ops->probe() == 0) found++;
        }
    }
    return found ? 0 : -1;
}
