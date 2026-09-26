/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/blk.c
 * Purpose: Block device registry + cached read/write dispatch.
 *
 * Every block device (ATA, virtio-blk, NVMe) registers here.  All reads
 * and writes go through the disk cache (blk_cache.c) so file systems do
 * not need to know about caching.
 */
#include "blk.h"
#include "blk_cache.h"
#include "heap.h"
#include "string.h"
#include "console.h"

static blk_device_t g_blk_devs[BLK_MAX_DEVICES];
static blk_stats_t  g_blk_stats[BLK_MAX_DEVICES];
static int          g_blk_count = 0;

void blk_init(void) {
    oc_memset(g_blk_devs, 0, sizeof(g_blk_devs));
    oc_memset(g_blk_stats, 0, sizeof(g_blk_stats));
    g_blk_count = 0;
}

int blk_register_device(const char *name, blk_type_t type,
                        u64 sectors, u32 sector_size,
                        const blk_ops_t *ops, void *priv) {
    if (g_blk_count >= BLK_MAX_DEVICES) return -1;
    int idx = -1;
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (!g_blk_devs[i].present) { idx = i; break; }
    }
    if (idx < 0) return -1;
    blk_device_t *d = &g_blk_devs[idx];
    oc_memset(d, 0, sizeof(*d));
    oc_strncpy(d->name, name, BLK_DEV_NAME_LEN - 1);
    d->name[BLK_DEV_NAME_LEN - 1] = 0;
    d->type = type;
    d->sectors = sectors;
    d->sector_size = sector_size ? sector_size : BLK_SECTOR_SIZE;
    d->present = 1;
    d->ops = ops;
    d->priv = priv;
    if (idx >= g_blk_count) g_blk_count = idx + 1;
    return idx;
}

int blk_unregister_device(int index) {
    if (index < 0 || index >= BLK_MAX_DEVICES) return -1;
    g_blk_devs[index].present = 0;
    return 0;
}

int blk_find_device(const char *name) {
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (g_blk_devs[i].present && oc_strncmp(g_blk_devs[i].name, name, BLK_DEV_NAME_LEN) == 0)
            return i;
    }
    return -1;
}

blk_device_t *blk_get_device(int index) {
    if (index < 0 || index >= BLK_MAX_DEVICES) return NULL;
    if (!g_blk_devs[index].present) return NULL;
    return &g_blk_devs[index];
}

int blk_read_sectors_raw(int dev_idx, u64 lba, u32 count, void *buf) {
    blk_device_t *d = blk_get_device(dev_idx);
    if (!d || !d->ops || !d->ops->read) return -1;
    int rc = d->ops->read(d, lba, count, buf);
    if (rc == 0) {
        g_blk_stats[dev_idx].reads++;
        g_blk_stats[dev_idx].read_sectors += count;
    }
    return rc;
}

int blk_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf) {
    blk_device_t *d = blk_get_device(dev_idx);
    if (!d || !d->ops || !d->ops->write) return -1;
    int rc = d->ops->write(d, lba, count, buf);
    if (rc == 0) {
        g_blk_stats[dev_idx].writes++;
        g_blk_stats[dev_idx].write_sectors += count;
    }
    return rc;
}

/* Cached read: try the cache first, fall back to raw read. */
int blk_read_sectors(int dev_idx, u64 lba, u32 count, void *buf) {
    /* For simplicity and correctness, we cache at the single-sector level.
     * File systems typically read one or a few sectors at a time. */
    u8 *out = (u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        if (blk_cache_read(dev_idx, lba + i, out + (u64)i * BLK_SECTOR_SIZE) != 0) {
            /* Cache miss: read from device. */
            if (blk_read_sectors_raw(dev_idx, lba + i, 1, out + (u64)i * BLK_SECTOR_SIZE) != 0)
                return -1;
            blk_cache_insert(dev_idx, lba + i, out + (u64)i * BLK_SECTOR_SIZE);
        }
    }
    return 0;
}

/* Cached write: update cache, mark dirty (write-back). */
int blk_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf) {
    const u8 *src = (const u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        blk_cache_write(dev_idx, lba + i, src + (u64)i * BLK_SECTOR_SIZE);
    }
    return 0;
}

void blk_list_devices(void) {
    char line[120]; char n[24];
    oc_console_puts("Block devices:\n");
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        if (!g_blk_devs[i].present) continue;
        blk_device_t *d = &g_blk_devs[i];
        const char *tn = "???";
        switch (d->type) {
            case BLK_TYPE_ATA:    tn = "ATA";    break;
            case BLK_TYPE_VIRTIO: tn = "virtio"; break;
            case BLK_TYPE_NVME:   tn = "NVMe";   break;
            case BLK_TYPE_AHCI:   tn = "AHCI";   break;
        }
        oc_strcpy(line, "  "); oc_strcat(line, d->name);
        oc_strcat(line, " ["); oc_strcat(line, tn); oc_strcat(line, "] ");
        oc_u64_to_str(d->sectors, n); oc_strcat(line, n);
        oc_strcat(line, " sectors (");
        oc_u64_to_str(d->sectors / 2, n); oc_strcat(line, n);
        oc_strcat(line, " KiB) sector_size=");
        oc_u64_to_str(d->sector_size, n); oc_strcat(line, n);
        oc_strcat(line, "\n");
        oc_console_puts(line);
    }
}

void blk_get_stats(int dev_idx, blk_stats_t *out) {
    if (dev_idx < 0 || dev_idx >= BLK_MAX_DEVICES || !out) return;
    *out = g_blk_stats[dev_idx];
}

int blk_num_devices(void) {
    int n = 0;
    for (int i = 0; i < BLK_MAX_DEVICES; i++)
        if (g_blk_devs[i].present) n++;
    return n;
}
