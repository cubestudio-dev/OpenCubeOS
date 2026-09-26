/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/fat32.c
 * Purpose: FAT32 file system implementation (read-focused).
 *
 * Layout (LBA28 sectors, 512 bytes each):
 *   [reserved region: boot sector + (reserved_sectors-1) more]
 *   [FAT #0: fat_size_sectors sectors]
 *   [FAT #1: fat_size_sectors sectors]   (we only use FAT #0)
 *   [data region: starts at data_start_lba, cluster 2 is the first one]
 *
 * FAT32 entries are 32 bits wide; the low 28 bits are the next-cluster
 * pointer. End-of-chain markers are 0x0FFFFFF8..0x0FFFFFFF. Free clusters
 * are 0x00000000.
 *
 * Directory entries are 32 bytes each. We support the classic 8.3 format
 * and a minimal LFN reader (collect LFN slots, assemble into a long name).
 */
#include "fat32.h"
#include "vfs.h"
#include "ata.h"
#include "console.h"
#include "heap.h"
#include "string.h"

/* ---- BPB / boot sector layout ---- */
typedef struct __attribute__((packed)) {
    u8  jmp[3];
    u8  oem[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  num_fats;
    u16 root_entries;
    u16 total_sectors16;
    u8  media;
    u16 fat_size16;
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors32;
    /* FAT32 extension. */
    u32 fat_size32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info_sector;
    u16 backup_boot_sector;
    u8  reserved[12];
    u8  drive_number;
    u8  reserved1;
    u8  boot_signature;
    u32 volume_id;
    u8  volume_label[11];
    u8  fs_type[8];
} fat32_bpb_t;

/* On-disk directory entry (32 bytes). */
typedef struct __attribute__((packed)) {
    u8  name[11];                  /* 8.3, space-padded */
    u8  attr;
    u8  nt_reserved;
    u8  creation_time_tenth;
    u16 creation_time;
    u16 creation_date;
    u16 last_access_date;
    u16 first_cluster_hi;
    u16 write_time;
    u16 write_date;
    u16 first_cluster_lo;
    u32 file_size;
} fat32_dirent_t;

/* LFN slot (32 bytes). */
typedef struct __attribute__((packed)) {
    u8  order;
    u16 name1[5];
    u8  attr;
    u8  type;
    u8  checksum;
    u16 name2[6];
    u16 first_cluster_lo;
    u16 name3[2];
} fat32_lfn_t;

/* Attributes. */
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

#define FAT_EOC            0x0FFFFFF8u
#define FAT_EOC_END        0x0FFFFFFFu
#define FAT_BAD_CLUSTER    0x0FFFFFF7u

/* ---- FS context ---- */
typedef struct {
    int   drive;
    u32   bytes_per_sector;
    u32   sectors_per_cluster;
    u32   bytes_per_cluster;
    u32   reserved_sectors;
    u32   num_fats;
    u32   fat_size_sectors;
    u32   root_cluster;
    u32   fat_start_lba;
    u32   data_start_lba;
    u32   total_clusters;
    u8   *fat_cache;             /* entire FAT #0 cached in RAM */
    u64   fat_cache_size;
} fat32_ctx_t;

/* Per-node private data. */
typedef struct {
    fat32_ctx_t *ctx;
    u32  start_cluster;          /* 0 for empty file */
    u64  size;                   /* bytes for files, 0 for dirs */
    int  is_dir;
} fat32_inode_t;

static vfs_fs_type_t  g_fat32_fs_type;
static vfs_fs_ops_t   g_fat32_fs_ops;
static vfs_file_ops_t g_fat32_file_ops;
static vfs_dir_ops_t  g_fat32_dir_ops;

/* Stats (last mounted ctx; for fat32_get_stats). */
static fat32_ctx_t *g_last_ctx = NULL;

/* ---- Helpers ---- */

/* Convert an 8.3 name (11 bytes, space-padded) to a displayable string.
 * "KERNEL  ELF" -> "KERNEL.ELF" */
static void fat32_format_short_name(const u8 *raw, char *out) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (raw[i] == ' ') break;
        out[p++] = (char)raw[i];
    }
    int has_ext = 0;
    for (int i = 8; i < 11; i++) {
        if (raw[i] != ' ') { has_ext = 1; break; }
    }
    if (has_ext) {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw[i] == ' ') break;
            out[p++] = (char)raw[i];
        }
    }
    out[p] = 0;
}

/* Parse a device string into a drive number 0..3.
 * Accepts "ata0".."ata3", "ata" (=0), or "0".."3". Returns -1 on bad input. */
static int fat32_parse_device(const char *device) {
    if (!device) return 0;
    if (oc_strcmp(device, "ata") == 0) return 0;
    if (oc_strncmp(device, "ata", 3) == 0) {
        char c = device[3];
        if (c >= '0' && c <= '3' && device[4] == 0) return c - '0';
        return -1;
    }
    /* Accept "hda".."hdd" (standard block device names). */
    if (oc_strncmp(device, "hd", 2) == 0) {
        char c = device[2];
        if (c >= 'a' && c <= 'd' && device[3] == 0) return c - 'a';
        return -1;
    }
    if (device[0] >= '0' && device[0] <= '3' && device[1] == 0) {
        return device[0] - '0';
    }
    return -1;
}

/* Read a whole cluster into buf (must be bytes_per_cluster bytes). */
static int fat32_read_cluster(fat32_ctx_t *ctx, u32 cluster, u8 *buf) {
    if (cluster < 2) return -1;
    u64 lba = (u64)ctx->data_start_lba + (u64)(cluster - 2) * (u64)ctx->sectors_per_cluster;
    int rc = ata_read_sectors(ctx->drive, lba, (int)ctx->sectors_per_cluster, buf);
    return rc == (int)ctx->sectors_per_cluster ? 0 : -1;
}

/* Return the next cluster in the chain, or 0 (== EOC) on end. */
static u32 fat32_next_cluster(fat32_ctx_t *ctx, u32 cluster) {
    if (!ctx->fat_cache) return 0;
    u64 off = (u64)cluster * 4;
    if (off + 4 > ctx->fat_cache_size) return 0;
    u32 v;
    oc_memcpy(&v, ctx->fat_cache + off, 4);
    return v & 0x0FFFFFFFu;
}

/* ---- FAT table / cluster helpers (write support) ---- */

/* Set a FAT entry's low 28 bits to `value`. Updates the in-memory cache and
 * writes the containing sector back to disk. The top 4 reserved bits of the
 * existing entry are preserved. Returns 0 on success, negative on error. */
static int fat32_set_fat_entry(fat32_ctx_t *ctx, u32 cluster, u32 value) {
    if (!ctx->fat_cache || cluster < 2) return -1;
    u64 off = (u64)cluster * 4;
    if (off + 4 > ctx->fat_cache_size) return -2;
    u32 oldv;
    oc_memcpy(&oldv, ctx->fat_cache + off, 4);
    u32 newv = (oldv & 0xF0000000u) | (value & 0x0FFFFFFFu);
    oc_memcpy(ctx->fat_cache + off, &newv, 4);
    u64 sector_offset = off / ctx->bytes_per_sector;
    int rc = ata_write_sectors(ctx->drive,
                               (u64)ctx->fat_start_lba + sector_offset, 1,
                               ctx->fat_cache + sector_offset * ctx->bytes_per_sector);
    return rc == 1 ? 0 : -3;
}

/* Allocate a free cluster from the FAT. Marks it as EOC (0x0FFFFFFF) and
 * returns the cluster number. Returns 0 on failure (no free cluster or I/O
 * error). */
static u32 fat32_alloc_cluster(fat32_ctx_t *ctx) {
    if (!ctx->fat_cache) return 0;
    u64 entries = ctx->fat_cache_size / 4;
    u64 limit = (u64)ctx->total_clusters + 2;
    if (limit > entries) limit = entries;
    for (u64 i = 2; i < limit; i++) {
        u32 v;
        oc_memcpy(&v, ctx->fat_cache + i * 4, 4);
        if ((v & 0x0FFFFFFFu) == 0) {
            if (fat32_set_fat_entry(ctx, (u32)i, FAT_EOC_END) < 0) return 0;
            return (u32)i;
        }
    }
    return 0;
}

/* Link `new_cluster` after `last_cluster` in the FAT chain. */
static int fat32_append_cluster(fat32_ctx_t *ctx, u32 last_cluster, u32 new_cluster) {
    return fat32_set_fat_entry(ctx, last_cluster, new_cluster);
}

/* Free every cluster in the chain starting at `start_cluster`. */
static int fat32_free_cluster_chain(fat32_ctx_t *ctx, u32 start_cluster) {
    u32 cluster = start_cluster;
    while (cluster >= 2 && cluster < FAT_EOC) {
        u32 next = fat32_next_cluster(ctx, cluster);
        if (fat32_set_fat_entry(ctx, cluster, 0x00000000u) < 0) return -1;
        cluster = next;
    }
    return 0;
}

/* Write a full cluster (bytes_per_cluster bytes) from `buf` to disk. */
static int fat32_write_cluster(fat32_ctx_t *ctx, u32 cluster, const u8 *buf) {
    if (cluster < 2) return -1;
    u64 lba = (u64)ctx->data_start_lba + (u64)(cluster - 2) * (u64)ctx->sectors_per_cluster;
    int rc = ata_write_sectors(ctx->drive, (u64)lba,
                               (int)ctx->sectors_per_cluster, buf);
    return rc == (int)ctx->sectors_per_cluster ? 0 : -1;
}

/* ---- Directory entry helpers (write support) ---- */

/* Read a 32-byte directory entry from disk. `dir_cluster` is the cluster
 * containing the entry; `entry_offset` is the byte offset within that cluster. */
static int fat32_read_dirent(fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset,
                             fat32_dirent_t *out) {
    if (!ctx || !out || dir_cluster < 2) return -1;
    if (entry_offset + 32 > ctx->bytes_per_cluster) return -2;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -3;
    if (fat32_read_cluster(ctx, dir_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -4;
    }
    oc_memcpy(out, cbuf + entry_offset, sizeof(*out));
    kfree(cbuf);
    return 0;
}

/* Write a 32-byte directory entry to disk (read-modify-write the cluster). */
static int fat32_write_dirent(fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset,
                              const fat32_dirent_t *in) {
    if (!ctx || !in || dir_cluster < 2) return -1;
    if (entry_offset + 32 > ctx->bytes_per_cluster) return -2;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -3;
    if (fat32_read_cluster(ctx, dir_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -4;
    }
    oc_memcpy(cbuf + entry_offset, in, sizeof(*in));
    int rc = fat32_write_cluster(ctx, dir_cluster, cbuf);
    kfree(cbuf);
    return rc;
}

/* Update a directory entry's first_cluster and size on disk. */
static int fat32_update_dir_entry(fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset,
                                  u32 new_first_cluster, u64 new_size) {
    fat32_dirent_t e;
    if (fat32_read_dirent(ctx, dir_cluster, entry_offset, &e) < 0) return -1;
    e.first_cluster_hi = (u16)(new_first_cluster >> 16);
    e.first_cluster_lo = (u16)(new_first_cluster & 0xFFFFu);
    e.file_size = (u32)new_size;
    return fat32_write_dirent(ctx, dir_cluster, entry_offset, &e);
}

/* Mark a directory entry as deleted (first byte = 0xE5). */
static int fat32_mark_entry_deleted(fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset) {
    fat32_dirent_t e;
    if (fat32_read_dirent(ctx, dir_cluster, entry_offset, &e) < 0) return -1;
    e.name[0] = 0xE5;
    return fat32_write_dirent(ctx, dir_cluster, entry_offset, &e);
}

/* Scan a directory for an entry matching `name` (8.3 display form). On success
 * returns 0 and fills the out_ params. Returns -1 if not found, -2 on I/O
 * error. */
/* P2-19 FIX: extract LFN name from collected LFN slots. */
static int fat32_extract_lfn_name(const u8 *lfn_buf, int lfn_count, char *out, int out_len) {
    /* LFN entries are stored in reverse order in lfn_buf. */
    char name[256];
    int name_pos = 0;
    for (int i = lfn_count - 1; i >= 0 && name_pos < 255; i--) {
        const u8 *slot = lfn_buf + i * 32;
        for (int j = 0; j < 5; j++) {
            u16 ch = (u16)slot[1 + j*2] | ((u16)slot[1 + j*2 + 1] << 8);
            if (ch == 0 || ch == 0xFFFF) break;
            if (name_pos < 255) name[name_pos++] = (char)(ch & 0xFF);
        }
        for (int j = 0; j < 6; j++) {
            u16 ch = (u16)slot[14 + j*2] | ((u16)slot[14 + j*2 + 1] << 8);
            if (ch == 0 || ch == 0xFFFF) break;
            if (name_pos < 255) name[name_pos++] = (char)(ch & 0xFF);
        }
        for (int j = 0; j < 2; j++) {
            u16 ch = (u16)slot[28 + j*2] | ((u16)slot[28 + j*2 + 1] << 8);
            if (ch == 0 || ch == 0xFFFF) break;
            if (name_pos < 255) name[name_pos++] = (char)(ch & 0xFF);
        }
    }
    name[name_pos] = 0;
    if (name_pos == 0) return -1;
    int copy = name_pos < out_len - 1 ? name_pos : out_len - 1;
    oc_memcpy(out, name, copy);
    out[copy] = 0;
    return 0;
}

static int fat32_find_entry(fat32_ctx_t *ctx, u32 dir_first_cluster, const char *name,
                            u32 *out_cluster, u32 *out_offset,
                            fat32_dirent_t *out_entry) {
    if (!ctx || dir_first_cluster < 2 || !name) return -2;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -2;
    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    u32 cluster = dir_first_cluster;
    int rc = -1;
    /* P2-19: LFN collection buffer. */
    u8 lfn_buf[20 * 32];
    int lfn_count = 0;
    while (cluster >= 2 && cluster < FAT_EOC) {
        if (fat32_read_cluster(ctx, cluster, cbuf) < 0) { rc = -2; break; }
        for (int i = 0; i < entries_per_cluster; i++) {
            fat32_dirent_t *e = (fat32_dirent_t *)(cbuf + i * 32);
            if (e->name[0] == 0x00) { rc = -1; goto done; }
            if (e->name[0] == 0xE5) { lfn_count = 0; continue; }
            if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                /* P2-19: collect LFN entries. */
                if (lfn_count < 20) {
                    oc_memcpy(lfn_buf + lfn_count * 32, e, 32);
                    lfn_count++;
                }
                continue;
            }
            if (e->attr & FAT_ATTR_VOLUME_ID) { lfn_count = 0; continue; }
            /* Try matching 8.3 name first. */
            char display[VFS_NAME_LEN];
            fat32_format_short_name(e->name, display);
            int matched = (oc_strcasecmp(display, name) == 0);
            /* P2-19: if no 8.3 match, try LFN name. */
            if (!matched && lfn_count > 0) {
                char lfn_name[VFS_NAME_LEN];
                if (fat32_extract_lfn_name(lfn_buf, lfn_count, lfn_name, sizeof(lfn_name)) == 0) {
                    if (oc_strcasecmp(lfn_name, name) == 0) matched = 1;
                }
            }
            if (matched) {
                if (out_cluster) *out_cluster = cluster;
                if (out_offset) *out_offset = (u32)(i * 32);
                if (out_entry) oc_memcpy(out_entry, e, sizeof(*out_entry));
                rc = 0;
                goto done;
            }
            lfn_count = 0;
        }
        cluster = fat32_next_cluster(ctx, cluster);
    }
done:
    kfree(cbuf);
    return rc;
}

/* Find a free slot in a directory for a new 32-byte entry (0x00 or 0xE5).
 * Does NOT extend the dir cluster chain if full. Returns 0 on success. */
/* P2-19 FIX: find N consecutive free directory entry slots.
 * Returns 0 on success, fills out_cluster and out_offset with the first slot. */
static int fat32_find_free_slots(fat32_ctx_t *ctx, u32 dir_first_cluster,
                                 int num_slots, u32 *out_cluster, u32 *out_offset) {
    if (!ctx || dir_first_cluster < 2 || !out_cluster || !out_offset || num_slots < 1) return -1;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -2;
    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    u32 cluster = dir_first_cluster;
    int rc = -1;
    while (cluster >= 2 && cluster < FAT_EOC) {
        if (fat32_read_cluster(ctx, cluster, cbuf) < 0) { rc = -2; break; }
        for (int i = 0; i <= entries_per_cluster - num_slots; i++) {
            int all_free = 1;
            for (int j = 0; j < num_slots; j++) {
                fat32_dirent_t *e = (fat32_dirent_t *)(cbuf + (i + j) * 32);
                if (e->name[0] != 0x00 && e->name[0] != 0xE5) { all_free = 0; break; }
            }
            if (all_free) {
                *out_cluster = cluster;
                *out_offset = (u32)(i * 32);
                rc = 0;
                goto done;
            }
        }
        cluster = fat32_next_cluster(ctx, cluster);
    }
done:
    kfree(cbuf);
    return rc;
}

/* Encode a display name (e.g. "FOO.TXT") into an 11-byte 8.3 FAT name
 * (space-padded, uppercased). Returns 0 on success, -1 if the name doesn't
 * fit in 8.3 format. */
static int fat32_encode_short_name(const char *name, u8 *out11) {
    oc_memset(out11, ' ', 11);
    int ni = 0;
    int oi = 0;
    while (name[ni] && name[ni] != '.' && oi < 8) {
        char c = name[ni];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out11[oi++] = (u8)c;
        ni++;
    }
    if (oi == 8 && name[ni] && name[ni] != '.') return -1;
    while (name[ni] && name[ni] != '.') ni++;
    if (name[ni] == '.') {
        ni++;
        oi = 8;
        while (name[ni] && oi < 11) {
            char c = name[ni];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out11[oi++] = (u8)c;
            ni++;
        }
        if (oi == 11 && name[ni]) return -1;
    }
    return 0;
}

/* P2-19 FIX: Check if a name needs LFN (doesn't fit 8.3 format).
 * Returns 1 if LFN needed, 0 if 8.3 suffices. */
static int fat32_needs_lfn(const char *name) {
    int ni = 0;
    int name_len = 0;
    int ext_len = 0;
    int has_ext = 0;
    /* Count name part (before dot). */
    while (name[ni] && name[ni] != '.') {
        char c = name[ni];
        if (c >= 'a' && c <= 'z') return 1;  /* lowercase needs LFN */
        name_len++;
        ni++;
    }
    if (name_len > 8) return 1;
    if (name[ni] == '.') {
        ni++;
        has_ext = 1;
        while (name[ni]) {
            char c = name[ni];
            if (c >= 'a' && c <= 'z') return 1;
            ext_len++;
            ni++;
        }
    } else {
        while (name[ni]) {
            char c = name[ni];
            if (c >= 'a' && c <= 'z') return 1;
            ni++;
        }
    }
    if (has_ext && ext_len > 3) return 1;
    return 0;
}

/* P2-19 FIX: Generate LFN entries for a long file name.
 * Each LFN entry is 32 bytes: seq(1) + name1[5*2] + attr(1) + type(1) +
 * checksum(1) + name2[6*2] + first_cluster(2) + name3[2*2].
 * Returns number of LFN entries created, or 0 on failure. */
static int fat32_write_lfn_entries(fat32_ctx_t *ctx, u32 dir_cluster,
                                    const char *name, u8 sfn_checksum,
                                    u32 start_offset, u8 *cbuf) {
    (void)dir_cluster;  /* P3: unused — LFN entries are written to the same cluster buffer */
    int name_len = oc_strlen(name);
    int num_lfn = (name_len + 12) / 13;  /* 13 chars per LFN entry */
    if (num_lfn > 20) return 0;  /* too long */

    /* Write LFN entries in reverse order (last entry first, with 0x40 flag). */
    for (int i = num_lfn - 1; i >= 0; i--) {
        u32 off = start_offset + (u32)(num_lfn - 1 - i) * 32;
        if (off + 32 > ctx->bytes_per_cluster) return 0;  /* simplified: single cluster */
        u8 *entry = cbuf + off;
        oc_memset(entry, 0, 32);
        entry[0] = (u8)(i + 1);  /* sequence number */
        if (i == num_lfn - 1) entry[0] |= 0x40;  /* last entry flag */
        entry[11] = 0x0F;  /* LFN attribute */
        entry[12] = 0;     /* type */
        entry[13] = sfn_checksum;  /* checksum of 8.3 name */

        /* Fill 13 UTF-16LE characters from the name. */
        int char_idx = i * 13;
        for (int j = 0; j < 13; j++) {
            u16 ch = 0;
            if (char_idx + j < name_len) ch = (u8)name[char_idx + j];
            else if (char_idx + j == name_len) ch = 0x0000;  /* null terminator */
            else ch = 0xFFFF;  /* padding */
            if (j < 5) {
                entry[1 + j*2] = (u8)(ch & 0xFF);
                entry[1 + j*2 + 1] = (u8)(ch >> 8);
            } else if (j < 11) {
                entry[14 + (j-5)*2] = (u8)(ch & 0xFF);
                entry[14 + (j-5)*2 + 1] = (u8)(ch >> 8);
            } else {
                entry[28 + (j-11)*2] = (u8)(ch & 0xFF);
                entry[28 + (j-11)*2 + 1] = (u8)(ch >> 8);
            }
        }
    }
    return num_lfn;
}

/* P2-19 FIX: Compute LFN checksum for a given 8.3 name. */
static u8 fat32_lfn_checksum(const u8 *short_name) {
    u8 sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (u8)((sum >> 1) | (sum << 7));
        sum += short_name[i];
    }
    return sum;
}

/* Create a new directory entry in `dir_first_cluster`. The entry has the given
 * name, attribute, first_cluster, and size. P2-19: now supports LFN.
 * Returns 0 on success. */
static int fat32_create_entry(fat32_ctx_t *ctx, u32 dir_first_cluster,
                              const char *name, u8 attr, u32 first_cluster, u64 size,
                              u32 *out_ec, u32 *out_eo) {
    u8 rawname[11];
    if (fat32_encode_short_name(name, rawname) < 0) return -1;

    /* P2-19: determine if we need LFN entries. */
    int needs_lfn = fat32_needs_lfn(name);
    int num_lfn = 0;
    if (needs_lfn) {
        /* Generate a unique short name (~1 suffix). */
        rawname[6] = '~'; rawname[7] = '1';
        rawname[8] = ' '; rawname[9] = ' '; rawname[10] = ' ';
        if (name[0] == '.') { rawname[0] = '_'; }
        /* Calculate number of LFN entries needed. */
        int name_len = oc_strlen(name);
        num_lfn = (name_len + 12) / 13;
        if (num_lfn > 20) return -1;
    }

    /* P2-19: find num_lfn + 1 consecutive free slots. */
    u32 slot_cluster = 0, slot_offset = 0;
    if (fat32_find_free_slots(ctx, dir_first_cluster, num_lfn + 1, &slot_cluster, &slot_offset) < 0) {
        return -2;
    }

    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -3;
    if (fat32_read_cluster(ctx, slot_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -4;
    }

    /* P2-19: write LFN entries before the 8.3 entry. */
    if (num_lfn > 0) {
        u8 cksum = fat32_lfn_checksum(rawname);
        int written = fat32_write_lfn_entries(ctx, slot_cluster, name, cksum,
                                               slot_offset, cbuf);
        if (written != num_lfn) {
            kfree(cbuf);
            return -6;
        }
    }

    /* Write the 8.3 entry after the LFN entries. */
    u32 sfn_offset = slot_offset + (u32)num_lfn * 32;
    fat32_dirent_t *e = (fat32_dirent_t *)(cbuf + sfn_offset);
    int was_end = (e->name[0] == 0x00);
    oc_memset(e, 0, sizeof(*e));
    oc_memcpy(e->name, rawname, 11);
    e->attr = attr;
    e->first_cluster_hi = (u16)(first_cluster >> 16);
    e->first_cluster_lo = (u16)(first_cluster & 0xFFFFu);
    e->file_size = (u32)size;
    /* BUG-036 FIX: Set creation/write timestamps.
     * Old code left time fields at 0, which FAT interprets as
     * 1980-00-00 00:00:00 (invalid date). Now we set a fixed
     * timestamp of 2026-01-01 00:00:00.
     * FAT date: (year-1980)<<9 | month<<5 | day
     * FAT time: hours<<11 | minutes<<5 | seconds/2 */
    e->creation_date = (46 << 9) | (1 << 5) | 1;  /* 2026-01-01 */
    e->creation_time = 0;                           /* 00:00:00 */
    e->write_date = e->creation_date;
    e->write_time = e->creation_time;
    e->last_access_date = e->creation_date;
    /* If we consumed the end-of-dir marker, write a new one in the next slot. */
    if (was_end && sfn_offset + 32 < ctx->bytes_per_cluster) {
        fat32_dirent_t *next = (fat32_dirent_t *)(cbuf + sfn_offset + 32);
        oc_memset(next, 0, sizeof(*next));
    }
    if (fat32_write_cluster(ctx, slot_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -5;
    }
    kfree(cbuf);
    if (out_ec) *out_ec = slot_cluster;
    if (out_eo) *out_eo = slot_offset;
    return 0;
}

/* Returns 1 if the directory is empty (no live entries), 0 if not empty,
 * -1 on I/O error. */
static int fat32_dir_is_empty(fat32_ctx_t *ctx, u32 dir_first_cluster) {
    if (!ctx || dir_first_cluster < 2) return -1;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -1;
    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    u32 cluster = dir_first_cluster;
    int result = 1;
    while (cluster >= 2 && cluster < FAT_EOC) {
        if (fat32_read_cluster(ctx, cluster, cbuf) < 0) { result = -1; break; }
        for (int i = 0; i < entries_per_cluster; i++) {
            fat32_dirent_t *e = (fat32_dirent_t *)(cbuf + i * 32);
            if (e->name[0] == 0x00) goto done;
            if (e->name[0] == 0xE5) continue;
            if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
            if (e->attr & FAT_ATTR_VOLUME_ID) continue;
            result = 0;
            goto done;
        }
        cluster = fat32_next_cluster(ctx, cluster);
    }
done:
    kfree(cbuf);
    return result;
}

/* ---- VFS file operations ---- */

static int fat32_open(vfs_node_t *node, int flags) {
    (void)flags;
    if (!node) return -1;
    fat32_inode_t *ino = (fat32_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    /* If the VFS patched this node's type to FILE but the on-disk entry is
     * still a DIRECTORY (happens when vfs_open's O_CREAT path calls
     * fat32_mkdir then patches the in-memory type to FILE), convert the
     * on-disk entry to a regular file: change the attribute to ARCHIVE and
     * reset the size to 0. The first_cluster is kept (the empty cluster
     * allocated by fat32_mkdir) so the first write can reuse it. */
    if (node->type == VFS_TYPE_FILE && ino->is_dir && node->parent) {
        fat32_ctx_t *ctx = ino->ctx;
        fat32_inode_t *pino = (fat32_inode_t *)node->parent->private;
        if (pino && pino->start_cluster >= 2) {
            u32 ec = 0, eo = 0;
            fat32_dirent_t e;
            if (fat32_find_entry(ctx, pino->start_cluster, node->name,
                                 &ec, &eo, &e) == 0) {
                e.attr = FAT_ATTR_ARCHIVE;
                e.file_size = 0;
                fat32_write_dirent(ctx, ec, eo, &e);
            }
        }
        ino->is_dir = 0;
        ino->size = 0;
        node->size = 0;
    }
    return 0;
}

static int fat32_read(vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    fat32_inode_t *ino = (fat32_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    if (ino->is_dir) return -3;
    if (ino->start_cluster < 2) return 0;     /* empty file */
    if (offset >= ino->size) return 0;
    u64 avail = ino->size - offset;
    int want = (int)(avail < (u64)size ? avail : (u64)size);

    fat32_ctx_t *ctx = ino->ctx;
    u8 *cluster_buf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cluster_buf) return -4;

    /* Walk the chain to find the cluster containing `offset`. */
    u32 cluster = ino->start_cluster;
    u64 bpc = ctx->bytes_per_cluster;
    u64 skip_clusters = offset / bpc;
    u64 off_in_cluster = offset % bpc;
    for (u64 i = 0; i < skip_clusters; i++) {
        cluster = fat32_next_cluster(ctx, cluster);
        if (cluster < 2 || cluster >= FAT_EOC) {
            kfree(cluster_buf);
            return 0;  /* chain shorter than expected */
        }
    }

    int copied = 0;
    while (copied < want) {
        if (cluster < 2 || cluster >= FAT_EOC) break;
        if (fat32_read_cluster(ctx, cluster, cluster_buf) < 0) break;
        u64 in_this = bpc - off_in_cluster;
        u64 need = (u64)(want - copied);
        u64 to_copy = in_this < need ? in_this : need;
        oc_memcpy((u8 *)buf + copied, cluster_buf + off_in_cluster, (usize)to_copy);
        copied += (int)to_copy;
        cluster = fat32_next_cluster(ctx, cluster);
        off_in_cluster = 0;
    }
    kfree(cluster_buf);
    return copied;
}

static int fat32_write(vfs_node_t *node, u64 offset, const void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    fat32_inode_t *ino = (fat32_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    if (ino->is_dir) return -3;
    fat32_ctx_t *ctx = ino->ctx;
    if (size == 0) return 0;
    u64 bpc = ctx->bytes_per_cluster;
    u8 *cluster_buf = (u8 *)kmalloc(bpc);
    if (!cluster_buf) return -4;

    /* Allocate the first cluster if the file is empty; zero it on disk so
     * unwritten tail bytes read as 0. */
    if (ino->start_cluster < 2) {
        u32 c = fat32_alloc_cluster(ctx);
        if (c < 2) { kfree(cluster_buf); return -5; }
        ino->start_cluster = c;
        oc_memset(cluster_buf, 0, (usize)bpc);
        if (fat32_write_cluster(ctx, c, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -6;
        }
    }

    /* Walk to the cluster containing `offset`, extending and zeroing new
     * intermediate clusters so gap reads return 0. */
    u32 cluster = ino->start_cluster;
    u64 skip_clusters = offset / bpc;
    u64 off_in_cluster = offset % bpc;
    for (u64 i = 0; i < skip_clusters; i++) {
        u32 next = fat32_next_cluster(ctx, cluster);
        if (next < 2 || next >= FAT_EOC) {
            u32 newc = fat32_alloc_cluster(ctx);
            if (newc < 2) { kfree(cluster_buf); return -7; }
            fat32_append_cluster(ctx, cluster, newc);
            oc_memset(cluster_buf, 0, (usize)bpc);
            if (fat32_write_cluster(ctx, newc, cluster_buf) < 0) {
                kfree(cluster_buf);
                return -8;
            }
            cluster = newc;
        } else {
            cluster = next;
        }
    }

    /* Write data cluster by cluster, extending the chain as needed. New
     * clusters allocated here are zeroed on disk first so unwritten tail
     * bytes (within the file size) read as 0. */
    int written = 0;
    u64 off = off_in_cluster;
    while (written < size) {
        if (fat32_read_cluster(ctx, cluster, cluster_buf) < 0) {
            oc_memset(cluster_buf, 0, (usize)bpc);
        }
        u64 in_this = bpc - off;
        u64 need = (u64)(size - written);
        u64 to_copy = in_this < need ? in_this : need;
        oc_memcpy(cluster_buf + off, (const u8 *)buf + written, (usize)to_copy);
        if (fat32_write_cluster(ctx, cluster, cluster_buf) < 0) break;
        written += (int)to_copy;
        off = 0;
        if (written < size) {
            u32 next = fat32_next_cluster(ctx, cluster);
            if (next < 2 || next >= FAT_EOC) {
                u32 newc = fat32_alloc_cluster(ctx);
                if (newc < 2) break;
                fat32_append_cluster(ctx, cluster, newc);
                oc_memset(cluster_buf, 0, (usize)bpc);
                if (fat32_write_cluster(ctx, newc, cluster_buf) < 0) break;
                cluster = newc;
            } else {
                cluster = next;
            }
        }
    }
    kfree(cluster_buf);

    /* Update file size. */
    u64 end_offset = offset + (u64)written;
    if (end_offset > ino->size) ino->size = end_offset;
    node->size = ino->size;

    /* Update the on-disk directory entry (first_cluster + size). */
    if (node->parent) {
        fat32_inode_t *pino = (fat32_inode_t *)node->parent->private;
        if (pino && pino->start_cluster >= 2) {
            u32 ec = 0, eo = 0;
            if (fat32_find_entry(ctx, pino->start_cluster, node->name,
                                 &ec, &eo, NULL) == 0) {
                fat32_update_dir_entry(ctx, ec, eo, ino->start_cluster, ino->size);
            }
        }
    }
    return written;
}

static u64 fat32_seek(vfs_node_t *node, u64 offset, int whence) {
    if (!node) return 0;
    u64 sz = node->size;
    u64 new_off = offset;
    switch (whence) {
        case VFS_SEEK_SET: new_off = offset; break;
        case VFS_SEEK_CUR: break;
        case VFS_SEEK_END: new_off = (sz > offset) ? (sz - offset) : 0; break;
        default: break;
    }
    if (new_off > sz) new_off = sz;
    return new_off;
}

static int fat32_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int fat32_stat(vfs_node_t *node, vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    oc_strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- VFS directory operations ---- */

static int fat32_mkdir(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fat32_inode_t *pino = (fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fat32_ctx_t *ctx = pino->ctx;

    /* Refuse if the name already exists. */
    if (fat32_find_entry(ctx, pino->start_cluster, name, NULL, NULL, NULL) == 0) {
        return -3;
    }

    /* Allocate a cluster for the new directory and zero it (empty dir). */
    u32 newc = fat32_alloc_cluster(ctx);
    if (newc < 2) return -4;
    u8 *zbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!zbuf) {
        fat32_set_fat_entry(ctx, newc, 0);  /* free the cluster */
        return -5;
    }
    oc_memset(zbuf, 0, (usize)ctx->bytes_per_cluster);
    if (fat32_write_cluster(ctx, newc, zbuf) < 0) {
        kfree(zbuf);
        fat32_set_fat_entry(ctx, newc, 0);
        return -6;
    }
    kfree(zbuf);

    /* Create the directory entry in the parent. */
    if (fat32_create_entry(ctx, pino->start_cluster, name, FAT_ATTR_DIRECTORY,
                           newc, 0, NULL, NULL) < 0) {
        fat32_set_fat_entry(ctx, newc, 0);  /* free the cluster */
        return -7;
    }
    return 0;
}

static int fat32_rmdir(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fat32_inode_t *pino = (fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fat32_ctx_t *ctx = pino->ctx;

    /* Find the entry. */
    u32 ec = 0, eo = 0;
    fat32_dirent_t e;
    if (fat32_find_entry(ctx, pino->start_cluster, name, &ec, &eo, &e) < 0) {
        return -3;  /* not found */
    }
    /* Must be a directory. */
    if (!(e.attr & FAT_ATTR_DIRECTORY)) {
        return -4;
    }
    /* Verify the dir is empty. */
    u32 fc = ((u32)e.first_cluster_hi << 16) | e.first_cluster_lo;
    if (fc >= 2) {
        int empty = fat32_dir_is_empty(ctx, fc);
        if (empty != 1) {
            return -5;  /* not empty (or I/O error) */
        }
        /* Free the dir's cluster chain. */
        fat32_free_cluster_chain(ctx, fc);
    }
    /* Mark the entry as deleted. */
    if (fat32_mark_entry_deleted(ctx, ec, eo) < 0) {
        return -6;
    }
    return 0;
}

static int fat32_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fat32_inode_t *pino = (fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fat32_ctx_t *ctx = pino->ctx;

    /* Find the entry. */
    u32 ec = 0, eo = 0;
    fat32_dirent_t e;
    if (fat32_find_entry(ctx, pino->start_cluster, name, &ec, &eo, &e) < 0) {
        return -3;  /* not found */
    }
    /* Refuse to unlink a directory (use rmdir for that). */
    if (e.attr & FAT_ATTR_DIRECTORY) {
        return -4;
    }
    /* Free the file's cluster chain. */
    u32 fc = ((u32)e.first_cluster_hi << 16) | e.first_cluster_lo;
    if (fc >= 2) {
        fat32_free_cluster_chain(ctx, fc);
    }
    /* Mark the entry as deleted. */
    if (fat32_mark_entry_deleted(ctx, ec, eo) < 0) {
        return -5;
    }
    return 0;
}

/* Read the entire directory into a kmalloc'd buffer of directory entries.
 * Returns the number of valid entries (excluding deleted & LFN), or a
 * negative value on error. `*out_buf` is kmalloc'd and must be kfree'd. */
static int fat32_read_dir_entries(fat32_inode_t *dir, fat32_dirent_t **out_buf) {
    if (!dir || !dir->is_dir || !dir->ctx) return -1;
    fat32_ctx_t *ctx = dir->ctx;

    /* Gather the directory's cluster chain into one buffer. FAT32 root
     * directories can grow; subdirectories can too. We bound the read at
     * 4096 entries = 128 KiB to keep memory in check. */
    int max_entries = 4096;
    fat32_dirent_t *buf = (fat32_dirent_t *)kmalloc((u64)max_entries * 32);
    if (!buf) return -2;
    int n = 0;
    u32 cluster = dir->start_cluster;
    if (cluster < 2) { *out_buf = buf; return 0; }

    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) { kfree(buf); return -3; }

    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    while (cluster >= 2 && cluster < FAT_EOC && n < max_entries) {
        if (fat32_read_cluster(ctx, cluster, cbuf) < 0) break;
        for (int i = 0; i < entries_per_cluster && n < max_entries; i++) {
            fat32_dirent_t *e = (fat32_dirent_t *)(cbuf + i * 32);
            oc_memcpy(&buf[n], e, 32);
            n++;
        }
        cluster = fat32_next_cluster(ctx, cluster);
    }
    kfree(cbuf);
    *out_buf = buf;
    return n;
}

/* Count free clusters in the FAT. */
static u64 fat32_count_free(fat32_ctx_t *ctx) {
    if (!ctx->fat_cache) return 0;
    u64 free_count = 0;
    u64 entries = ctx->fat_cache_size / 4;
    for (u64 i = 0; i < entries; i++) {
        u32 v;
        oc_memcpy(&v, ctx->fat_cache + i * 4, 4);
        if ((v & 0x0FFFFFFFu) == 0) free_count++;
    }
    return free_count;
}

static int fat32_readdir(vfs_node_t *dir, int index, vfs_dirent_t *entry) {
    if (!dir || !entry) return -1;
    fat32_inode_t *ino = (fat32_inode_t *)dir->private;
    if (!ino) return -2;

    fat32_dirent_t *buf = NULL;
    int n = fat32_read_dir_entries(ino, &buf);
    if (n < 0) return -3;

    /* Walk entries, skipping deleted (0xE5), end-of-dir (0x00), LFN slots,
     * and volume-label entries. Map the index-th surviving entry. */
    int shown = 0;
    int rc = -4;
    for (int i = 0; i < n; i++) {
        fat32_dirent_t *e = &buf[i];
        if (e->name[0] == 0x00) break;            /* end of dir */
        if (e->name[0] == 0xE5) continue;          /* deleted */
        if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;  /* LFN slot */
        if (e->attr & FAT_ATTR_VOLUME_ID) continue; /* volume label */
        if (shown == index) {
            oc_memset(entry, 0, sizeof(*entry));
            fat32_format_short_name(e->name, entry->name);
            entry->type = (e->attr & FAT_ATTR_DIRECTORY) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            u32 cluster = ((u32)e->first_cluster_hi << 16) | e->first_cluster_lo;
            entry->inode = cluster;
            rc = 0;
            break;
        }
        shown++;
    }
    kfree(buf);
    return rc;
}

static vfs_node_t *fat32_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    fat32_inode_t *pino = (fat32_inode_t *)parent->private;
    if (!pino) return NULL;

    fat32_dirent_t *buf = NULL;
    int n = fat32_read_dir_entries(pino, &buf);
    if (n < 0) return NULL;

    vfs_node_t *result = NULL;
    /* P2-19: collect LFN entries for long name matching. */
    u8 lfn_buf[20 * 32];
    int lfn_count = 0;
    for (int i = 0; i < n; i++) {
        fat32_dirent_t *e = &buf[i];
        if (e->name[0] == 0x00) break;
        if (e->name[0] == 0xE5) { lfn_count = 0; continue; }
        if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
            /* P2-19: collect LFN entries. */
            if (lfn_count < 20) {
                oc_memcpy(lfn_buf + lfn_count * 32, e, 32);
                lfn_count++;
            }
            continue;
        }
        if (e->attr & FAT_ATTR_VOLUME_ID) { lfn_count = 0; continue; }

        char display[VFS_NAME_LEN];
        fat32_format_short_name(e->name, display);

        /* P2-19: try 8.3 match first, then LFN match. */
        int matched = (oc_strcasecmp(display, name) == 0);
        if (!matched && lfn_count > 0) {
            char lfn_name[VFS_NAME_LEN];
            if (fat32_extract_lfn_name(lfn_buf, lfn_count, lfn_name, sizeof(lfn_name)) == 0) {
                if (oc_strcasecmp(lfn_name, name) == 0) {
                    matched = 1;
                    /* Use the LFN name as the display name. */
                    oc_strncpy(display, lfn_name, VFS_NAME_LEN - 1);
                    display[VFS_NAME_LEN - 1] = 0;
                }
            }
        }
        if (matched) {
            fat32_inode_t *child_ino = (fat32_inode_t *)kmalloc(sizeof(fat32_inode_t));
            if (child_ino) {
                oc_memset(child_ino, 0, sizeof(*child_ino));
                child_ino->ctx = pino->ctx;
                child_ino->start_cluster = ((u32)e->first_cluster_hi << 16) | e->first_cluster_lo;
                child_ino->is_dir = (e->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
                child_ino->size = e->file_size;
                vfs_node_t *cn = vfs_alloc_node(display,
                                                child_ino->is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE,
                                                &g_fat32_fs_type);
                if (cn) {
                    cn->private = child_ino;
                    cn->size = e->file_size;
                    cn->parent = parent;
                    result = cn;
                } else {
                    kfree(child_ino);
                }
            }
            break;
        }
    }
    kfree(buf);
    return result;
}

/* ---- Mount / unmount ---- */

static vfs_node_t *fat32_fs_mount(const char *device) {
    int drive = fat32_parse_device(device);
    if (drive < 0) {
        oc_console_puts("fat32: invalid device string\n");
        return NULL;
    }
    if (!ata_detect(drive)) {
        oc_console_puts("fat32: drive not present\n");
        return NULL;
    }

    /* Read the boot sector. */
    u8 boot[512];
    if (ata_read_sectors(drive, 0, 1, boot) != 1) {
        oc_console_puts("fat32: read boot sector failed\n");
        return NULL;
    }
    fat32_bpb_t *bpb = (fat32_bpb_t *)boot;

    /* Sanity check. */
    if (bpb->bytes_per_sector == 0 ||
        bpb->sectors_per_cluster == 0 ||
        bpb->bytes_per_sector > 4096) {
        oc_console_puts("fat32: invalid BPB\n");
        return NULL;
    }
    /* Distinguish FAT32 from FAT12/16: root_entries == 0 and fat_size16 == 0. */
    if (bpb->root_entries != 0 || bpb->fat_size16 != 0) {
        oc_console_puts("fat32: not a FAT32 volume (FAT12/16 detected)\n");
        return NULL;
    }
    if (bpb->fat_size32 == 0) {
        oc_console_puts("fat32: fat_size32 == 0\n");
        return NULL;
    }

    fat32_ctx_t *ctx = (fat32_ctx_t *)kmalloc(sizeof(fat32_ctx_t));
    if (!ctx) return NULL;
    oc_memset(ctx, 0, sizeof(*ctx));
    ctx->drive = (int)drive;
    ctx->bytes_per_sector = bpb->bytes_per_sector;
    ctx->sectors_per_cluster = bpb->sectors_per_cluster;
    ctx->bytes_per_cluster = (u32)bpb->bytes_per_sector * bpb->sectors_per_cluster;
    ctx->reserved_sectors = bpb->reserved_sectors;
    ctx->num_fats = bpb->num_fats;
    ctx->fat_size_sectors = bpb->fat_size32;
    ctx->root_cluster = bpb->root_cluster;
    ctx->fat_start_lba = ctx->reserved_sectors;
    ctx->data_start_lba = ctx->reserved_sectors + ctx->num_fats * ctx->fat_size_sectors;
    /* total_clusters = (total_sectors - data_start_lba) / sectors_per_cluster.
     * We approximate using total_sectors32 (preferred for FAT32). */
    u32 total_sectors = bpb->total_sectors32 ? bpb->total_sectors32 : (u32)bpb->total_sectors16;
    if (total_sectors > ctx->data_start_lba) {
        ctx->total_clusters = (total_sectors - ctx->data_start_lba) / ctx->sectors_per_cluster;
    }

    /* Cache the FAT. */
    ctx->fat_cache_size = (u64)ctx->fat_size_sectors * ctx->bytes_per_sector;
    ctx->fat_cache = (u8 *)kmalloc(ctx->fat_cache_size);
    if (!ctx->fat_cache) {
        oc_console_puts("fat32: FAT cache alloc failed\n");
        kfree(ctx);
        return NULL;
    }
    int rc = ata_read_sectors(drive, (u64)ctx->fat_start_lba,
                              (int)ctx->fat_size_sectors, ctx->fat_cache);
    if (rc != (int)ctx->fat_size_sectors) {
        oc_console_puts("fat32: FAT read failed\n");
        kfree(ctx->fat_cache);
        kfree(ctx);
        return NULL;
    }

    g_last_ctx = ctx;

    /* Create the root VFS node. */
    fat32_inode_t *root_ino = (fat32_inode_t *)kmalloc(sizeof(fat32_inode_t));
    if (!root_ino) {
        kfree(ctx->fat_cache);
        kfree(ctx);
        return NULL;
    }
    oc_memset(root_ino, 0, sizeof(*root_ino));
    root_ino->ctx = ctx;
    root_ino->start_cluster = ctx->root_cluster;
    root_ino->is_dir = 1;
    root_ino->size = 0;

    vfs_node_t *root = vfs_alloc_node("", VFS_TYPE_DIR, &g_fat32_fs_type);
    if (!root) {
        kfree(root_ino);
        kfree(ctx->fat_cache);
        kfree(ctx);
        return NULL;
    }
    root->private = root_ino;
    return root;
}

/* BUG-027 FIX: Recursively free all VFS nodes and inodes in the tree.
 * Old code only freed the root node, leaking all child nodes/inodes
 * allocated during directory traversal. This function walks the
 * entire tree and frees everything. */
static void fat32_free_subtree(vfs_node_t *node) {
    if (!node) return;
    /* Recursively free children first. */
    vfs_node_t *child = node->first_child;
    while (child) {
        vfs_node_t *next = child->next_sibling;
        fat32_free_subtree(child);
        child = next;
    }
    /* Free this node's private data (inode). */
    fat32_inode_t *ino = (fat32_inode_t *)node->private;
    if (ino) kfree(ino);
    /* Free the node itself. */
    kfree(node);
}

static int fat32_fs_unmount(vfs_node_t *root) {
    if (!root) return -1;
    /* BUG-027 FIX: Free the entire VFS subtree, not just the root. */
    fat32_inode_t *ino = (fat32_inode_t *)root->private;
    if (ino) {
        fat32_ctx_t *ctx = ino->ctx;
        if (ctx) {
            if (ctx->fat_cache) kfree(ctx->fat_cache);
            kfree(ctx);
        }
    }
    /* Recursively free all nodes + inodes in the tree. */
    fat32_free_subtree(root);
    return 0;
}

/* ---- Init ---- */

void fat32_init(void) {
    oc_memset(&g_fat32_fs_type, 0, sizeof(g_fat32_fs_type));
    oc_strncpy(g_fat32_fs_type.name, "fat32", sizeof(g_fat32_fs_type.name) - 1);
    g_fat32_fs_ops.mount   = fat32_fs_mount;
    g_fat32_fs_ops.unmount = fat32_fs_unmount;
    g_fat32_file_ops.open  = fat32_open;
    g_fat32_file_ops.read  = fat32_read;
    g_fat32_file_ops.write = fat32_write;
    g_fat32_file_ops.seek  = fat32_seek;
    g_fat32_file_ops.close = fat32_close;
    g_fat32_file_ops.stat  = fat32_stat;
    g_fat32_dir_ops.mkdir   = fat32_mkdir;
    g_fat32_dir_ops.rmdir   = fat32_rmdir;
    g_fat32_dir_ops.readdir = fat32_readdir;
    g_fat32_dir_ops.lookup  = fat32_lookup;
    g_fat32_dir_ops.unlink  = fat32_unlink;
    /* rename is not supported; the VFS layer falls back to copy+unlink. */
    g_fat32_fs_type.fs_ops   = &g_fat32_fs_ops;
    g_fat32_fs_type.file_ops = &g_fat32_file_ops;
    g_fat32_fs_type.dir_ops  = &g_fat32_dir_ops;

    vfs_register_fs("fat32", &g_fat32_fs_ops, &g_fat32_file_ops, &g_fat32_dir_ops);
    oc_console_puts("fat32: registered (read/write)\n");
}

int fat32_mount(const char *device, const char *mount_point) {
    return vfs_mount("fat32", mount_point, device);
}

void fat32_get_stats(u64 *total_sectors, u64 *free_clusters, u32 *cluster_size) {
    if (total_sectors) *total_sectors = 0;
    if (free_clusters) *free_clusters = 0;
    if (cluster_size)  *cluster_size  = 0;
    if (!g_last_ctx) return;
    if (total_sectors) *total_sectors = (u64)g_last_ctx->total_clusters * g_last_ctx->sectors_per_cluster;
    if (free_clusters) *free_clusters = fat32_count_free(g_last_ctx);
    if (cluster_size)  *cluster_size  = g_last_ctx->bytes_per_cluster;
}
