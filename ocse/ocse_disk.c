/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_disk.c
 * Purpose: disk identification for the installer: block-device
 * enumeration, MBR/GPT table classification, per-partition filesystem
 * signature detection and the low-capacity notice.
 *
 * Signatures are read from the partition's first sectors:
 *   NTFS  : boot sector offset 0x03 = "NTFS    "
 *   exFAT : boot sector offset 0x03 = "EXFAT   "
 *   FAT32 : boot sector offset 0x52 = "FAT32   "
 *   FAT16 : boot sector offset 0x36 = "FAT16   "
 *   ext*  : superblock magic 0xEF53 at byte 1024+56 (sector 2, off 0x38)
 * GPT partitions are additionally classified by their type GUID
 * (ESP / Linux filesystem data). The MBR type byte is the fallback
 * when the boot sector carries no recognizable signature.
 */
#include "ocse_disk.h"
#include "lib_string.h"
#include "driver_block_blk.h"
#include "driver_block_part.h"
#include <stdint.h>

ocse_disk_t g_ocse_disks[OCSE_MAX_DISKS];
int g_ocse_ndisks = 0;

const char* ocse_fs_name(ocse_fs_t fs) {
    switch (fs) {
    case OCSE_FS_FAT32: return "FAT32";
    case OCSE_FS_EXFAT: return "exFAT";
    case OCSE_FS_NTFS:  return "NTFS";
    case OCSE_FS_EXT:   return "ext";
    case OCSE_FS_ESP:   return "ESP";
    case OCSE_FS_SWAP:  return "swap";
    default:            return "?";
    }
}

static int bytes_eq(const u8* a, const char* s, u32 n) {
    for (u32 i = 0; i < n; i++)
        if (a[i] != (u8)s[i]) return 0;
    return 1;
}

/* GPT type GUIDs as stored on disk (mixed-endian first 3 fields). */
static const u8 g_guid_esp[16] = {           /* C12A7328-F80F-4D68-... */
    0x28, 0x73, 0x2A, 0xC1, 0x0F, 0xF8, 0x68, 0x4D,
    0xB8, 0xED, 0x00, 0x50, 0x7C, 0x25, 0x92, 0x98
};
static const u8 g_guid_linux_fs[16] = {      /* 0FC63DAF-8483-4772-... */
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

static int guid_is(const u8* g, const u8* want) {
    for (int i = 0; i < 16; i++)
        if (g[i] != want[i]) return 0;
    return 1;
}

/* Read the first sectors of a partition and classify the filesystem. */
static ocse_fs_t sniff_fs(int dev_idx, u64 start_lba) {
    static u8 sec[1536];                     /* three sectors: the ext2/3/4
                                              * superblock magic sits at byte
                                              * 1024+56 = 1080, in sector 3. */
    u64 lba = start_lba ? start_lba : 0;
    int got = -1;
    if (driver_block_read_sectors(dev_idx, lba, 3, sec) == 0)
        got = 3;
    else if (driver_block_read_sectors(dev_idx, lba, 2, sec) == 0)
        got = 2;
    else if (driver_block_read_sectors(dev_idx, lba, 1, sec) == 0)
        got = 1;
    if (got < 1)
        return OCSE_FS_UNKNOWN;
    /* NTFS / exFAT / FAT at the boot sector. */
    if (bytes_eq(sec + 0x03, "NTFS    ", 8)) return OCSE_FS_NTFS;
    if (bytes_eq(sec + 0x03, "EXFAT   ", 8)) return OCSE_FS_EXFAT;
    if (bytes_eq(sec + 0x52, "FAT32   ", 8)) return OCSE_FS_FAT32;
    if (bytes_eq(sec + 0x36, "FAT16   ", 8)) return OCSE_FS_FAT32;
    /* ext2/3/4 superblock magic (little-endian 0xEF53) at byte 0x438. */
    if (got >= 3 && sec[0x438] == 0x53 && sec[0x439] == 0xEF)
        return OCSE_FS_EXT;
    return OCSE_FS_UNKNOWN;
}

/* Classify one partition: signature first, GUID/type-byte fallback. */
static ocse_fs_t classify_part(int dev_idx, const partition_t* pt) {
    ocse_fs_t fs = sniff_fs(dev_idx, pt->start_lba);
    if (fs != OCSE_FS_UNKNOWN)
        return fs;
    if (pt->gpt_type_guid[0] || pt->gpt_type_guid[1]) {
        if (guid_is(pt->gpt_type_guid, g_guid_esp))       return OCSE_FS_ESP;
        if (guid_is(pt->gpt_type_guid, g_guid_linux_fs))  return OCSE_FS_EXT;
        return OCSE_FS_UNKNOWN;
    }
    switch (pt->type) {
    case 0x07: return OCSE_FS_NTFS;          /* NTFS/exFAT/HPSF (generic) */
    case 0x0B: case 0x0C: return OCSE_FS_FAT32;
    case 0x0E: case 0x04: case 0x06: return OCSE_FS_FAT32;   /* FAT16 */
    case 0x82: return OCSE_FS_SWAP;
    case 0x83: return OCSE_FS_EXT;
    default:   return OCSE_FS_UNKNOWN;
    }
}

/* Partition children register as "<parent>pN" block devices (the
 * v1.0.0 stage-22 scan). They are VIEWS of a disk, not disks: the
 * installer lists physical tables only, so a name that ends in 'p'
 * plus digits AND whose prefix names another device is skipped. */
static int is_partition_child(int idx, int total) {
    driver_block_device_t* self = driver_block_get_device(idx);
    if (!self) return 0;
    const char* nm = self->name;
    u32 len = 0;
    while (nm[len]) len++;
    /* strip trailing digits */
    u32 end = len;
    while (end > 0 && nm[end - 1] >= '0' && nm[end - 1] <= '9') end--;
    if (end == 0 || end == len) return 0;      /* no digits at all */
    if (end > 0 && nm[end - 1] != 'p') return 0;   /* not pN */
    u32 plen = end - 1;                        /* parent name length */
    if (plen == 0) return 0;
    for (int j = 0; j < total; j++) {
        if (j == idx) continue;
        driver_block_device_t* other = driver_block_get_device(j);
        if (!other || !other->present) continue;
        if (strlen(other->name) != plen) continue;
        if (memcmp(other->name, nm, plen) == 0)
            return 1;                          /* nm = other + "pN" */
    }
    return 0;
}

void ocse_disk_scan(void) {
    g_ocse_ndisks = 0;
    for (int i = 0; i < OCSE_MAX_DISKS; i++) {
        g_ocse_disks[i].present = 0;
        g_ocse_disks[i].nparts = 0;
    }
    int n = driver_block_num_devices();
    for (int i = 0; i < n && g_ocse_ndisks < OCSE_MAX_DISKS; i++) {
        driver_block_device_t* dev = driver_block_get_device(i);
        if (!dev || !dev->present) continue;
        if (is_partition_child(i, n)) continue;
        ocse_disk_t* d = &g_ocse_disks[g_ocse_ndisks++];
        d->present = 1;
        for (int k = 0; k < 15 && dev->name[k]; k++)
            d->name[k] = dev->name[k];
        d->name[15] = 0;
        d->sectors = dev->sectors;
        d->mb = (dev->sectors * (u64)dev->sector_size) / (1024u * 1024u);
        d->space_low = d->mb < OCSE_MIN_DISK_MB;
        d->table_kind = 0;
        d->nparts = 0;
        driver_block_part_table_t tbl;
        if (driver_block_part_parse(i, &tbl) == 0) {
            d->table_kind = (tbl.table_type == PART_TYPE_GPT) ? 2 : 1;
            for (int p = 0; p < tbl.count && d->nparts < OCSE_MAX_PARTS; p++) {
                partition_t* pt = &tbl.parts[p];
                if (!pt->present) continue;
                ocse_part_t* op = &d->parts[d->nparts++];
                op->start_lba = pt->start_lba;
                op->sectors = pt->sectors;
                op->mb = (pt->sectors * 512u) / (1024u * 1024u);
                op->mbr_type = pt->type;
                op->fs = classify_part(i, pt);
            }
        }
    }
}
