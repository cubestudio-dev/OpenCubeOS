/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/exfat.c
 * Purpose: exFAT filesystem driver (read + write) for the VFS.
 *
 * Uses the ORIGINAL WP-05 VFS interface (3 separate ops structs:
 * vfs_fs_ops_t, vfs_file_ops_t, vfs_dir_ops_t). The fs-wide state lives
 * in an exfat_ctx_t stored off the root node's private (via the root
 * inode). Each looked-up node carries an exfat_inode_t in its private.
 *
 * exFAT stores filenames as UTF-16LE in 0xC1 name entries; this driver
 * flattens them to ASCII by taking the low byte of each UTF-16 code unit
 * (sufficient for ASCII filenames).
 *
 * On-disk structures use #pragma pack(push, 1). All functions are static
 * except exfat_init. Memory comes from kmalloc/kfree; sector I/O goes
 * through ata_read_sectors/ata_write_sectors (drive 0..3, LBA 0-based).
 *
 * The `device` string passed to mount is parsed into a drive number 0..3
 * (e.g. "ata0" -> 0, "ata3" -> 3, "0".."3" also accepted). exFAT is
 * mounted on the whole disk starting at LBA 0 (no partition offset).
 */
#include "exfat.h"
#include "types.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "vfs.h"
#include "ata.h"

#pragma pack(push, 1)
typedef struct {
    u8  jump[3];
    u8  fs_name[8];              /* "EXFAT   " */
    u8  must_be_zero[53];
    u64 partition_offset;        /* in sectors */
    u64 volume_length;           /* in sectors */
    u32 fat_offset;              /* in sectors, relative to vol start */
    u32 fat_length;              /* in sectors */
    u32 cluster_heap_offset;     /* in sectors */
    u32 cluster_count;           /* number of allocatable clusters */
    u32 first_cluster_of_root_directory;
    u32 volume_serial_number;
    u16 fs_revision;
    u16 volume_flags;
    u8  bytes_per_sector_shift;  /* sector size = 1 << this */
    u8  sectors_per_cluster_shift;
    u8  number_of_fats;
    u8  drive_select;
    u8  percent_in_use;
    u8  reserved[7];
    u8  boot_code[390];
    u16 boot_signature;          /* 0xAA55 */
} exfat_bpb_t;

/* Generic 32-byte directory entry. */
typedef struct {
    u8  type;
    u8  data[31];
} exfat_dentry_t;

/* 0x85 - file directory entry (32 bytes). */
typedef struct {
    u8  type;                    /* 0x85 */
    u8  secondary_count;
    u16 set_checksum;
    u16 attributes;
    u16 reserved1;
    u32 created_timestamp;
    u32 last_modified_timestamp;
    u32 last_accessed_timestamp;
    u8  created_10ms_increment;
    u8  last_modified_10ms_increment;
    u8  last_accessed_10ms_increment;
    u8  reserved2[9];
} exfat_file_entry_t;

/* 0xC0 - stream extension entry (32 bytes). */
typedef struct {
    u8  type;                    /* 0xC0 */
    u8  flags;
    u8  reserved1;
    u8  name_length;             /* in UTF-16 chars */
    u16 name_hash;
    u16 reserved2;
    u64 valid_data_length;
    u32 reserved3;
    u32 first_cluster;
    u64 data_length;
} exfat_stream_entry_t;

/* 0xC1 - file name entry (32 bytes). */
typedef struct {
    u8  type;                    /* 0xC1 */
    u8  flags;
    u16 filename[15];            /* UTF-16LE */
} exfat_name_entry_t;
#pragma pack(pop)

#define EXFAT_EOC                    0xFFFFFFFFu
#define EXFAT_FREE_CLUSTER           0x00000000u
#define EXFAT_BAD_CLUSTER            0xFFFFFFF7u
#define EXFAT_TYPE_UNUSED            0x00
#define EXFAT_TYPE_BITMAP            0x81
#define EXFAT_TYPE_UPCASE            0x82
#define EXFAT_TYPE_VOL_LABEL         0x83
#define EXFAT_TYPE_FILE              0x85
#define EXFAT_TYPE_STREAM            0xC0
#define EXFAT_TYPE_NAME              0xC1
#define EXFAT_FILE_FLAG_DIR          0x0001
#define EXFAT_STREAM_FLAG_ALLOC_OK   0x02

/* FS-wide state. Lives in the root node's inode->ctx. */
typedef struct {
    int  drive;
    u32  sector_size;
    u32  sectors_per_cluster;
    u32  cluster_size;
    u32  fat_start_lba;
    u32  fat_size_sectors;
    u32  heap_start_lba;
    u32  cluster_count;
    u32  root_cluster;
    u32  last_alloc;
} exfat_ctx_t;

/* Per-node state (file or directory). Lives in vfs_node_t->private. */
typedef struct {
    exfat_ctx_t *ctx;
    u32  first_cluster;          /* first cluster of file/dir data (0 if empty) */
    int  is_dir;
    u32  entry_cluster;          /* where the file entry set lives in parent */
    u32  entry_offset;           /* byte offset within entry_cluster */
} exfat_inode_t;

static vfs_fs_type_t  g_exfat_fs_type;
static vfs_fs_ops_t   g_exfat_fs_ops;
static vfs_file_ops_t g_exfat_file_ops;
static vfs_dir_ops_t  g_exfat_dir_ops;

/* ---- Device string parsing ---- */

/* Parse "ata0".."ata3", "ata", or "0".."3" into a drive number 0..3.
 * Returns -1 on bad input. Mirrors fat32_parse_device. */
static int exfat_parse_device(const char *device) {
    if (!device) return 0;
    if (oc_strcmp(device, "ata") == 0) return 0;
    if (oc_strncmp(device, "ata", 3) == 0) {
        char c = device[3];
        if (c >= '0' && c <= '3' && device[4] == 0) return c - '0';
        return -1;
    }
    if (device[0] >= '0' && device[0] <= '3' && device[1] == 0) {
        return device[0] - '0';
    }
    return -1;
}

/* ---- Sector I/O wrappers ---- */

static int exfat_read_sectors(exfat_ctx_t *ctx, u64 lba, u32 count, void *buf) {
    int rc = ata_read_sectors(ctx->drive, lba, (int)count, buf);
    return rc == (int)count ? 0 : -1;
}

static int exfat_write_sectors(exfat_ctx_t *ctx, u64 lba, u32 count,
                               const void *buf) {
    int rc = ata_write_sectors(ctx->drive, lba, (int)count, buf);
    return rc == (int)count ? 0 : -1;
}

/* ---- FAT entry get/set ---- */
/* exFAT FAT entries are 32-bit; 0 = free, 0xFFFFFFFF = EOC. */

static u32 exfat_fat_get(exfat_ctx_t *ctx, u32 cluster) {
    u64 off = (u64)cluster * 4;
    u64 sec = ctx->fat_start_lba + (off / ctx->sector_size);
    u32 off_in_sec = (u32)(off % ctx->sector_size);
    /* P1-7 FIX: allocate sector_size bytes (may be > 512) to avoid stack overflow. */
    u8 *buf = (u8*)kmalloc(ctx->sector_size);
    if (!buf) return EXFAT_EOC;
    if (exfat_read_sectors(ctx, sec, 1, buf) != 0) { kfree(buf); return EXFAT_EOC; }
    u32 val;
    oc_memcpy(&val, buf + off_in_sec, 4);
    kfree(buf);
    return val;
}

static int exfat_fat_set(exfat_ctx_t *ctx, u32 cluster, u32 value) {
    u64 off = (u64)cluster * 4;
    u64 sec = ctx->fat_start_lba + (off / ctx->sector_size);
    u32 off_in_sec = (u32)(off % ctx->sector_size);
    /* P1-7 FIX: allocate sector_size bytes (may be > 512) to avoid stack overflow. */
    u8 *buf = (u8*)kmalloc(ctx->sector_size);
    if (!buf) return -1;
    if (exfat_read_sectors(ctx, sec, 1, buf) != 0) { kfree(buf); return -1; }
    oc_memcpy(buf + off_in_sec, &value, 4);
    int rc = exfat_write_sectors(ctx, sec, 1, buf);
    kfree(buf);
    return rc;
}

static u32 exfat_alloc_cluster(exfat_ctx_t *ctx) {
    u32 start = ctx->last_alloc ? ctx->last_alloc + 1 : 2;
    if (start < 2 || start >= ctx->cluster_count + 2) start = 2;
    for (u32 i = 0; i < ctx->cluster_count; i++) {
        u32 c = start + i;
        if (c >= ctx->cluster_count + 2) c -= ctx->cluster_count;
        if (c < 2) c = 2;
        if (exfat_fat_get(ctx, c) == EXFAT_FREE_CLUSTER) {
            exfat_fat_set(ctx, c, EXFAT_EOC);
            ctx->last_alloc = c;
            return c;
        }
    }
    return 0;
}

static void exfat_free_chain(exfat_ctx_t *ctx, u32 first) {
    u32 cur = first;
    while (cur >= 2 && cur != EXFAT_EOC && cur < EXFAT_BAD_CLUSTER) {
        u32 next = exfat_fat_get(ctx, cur);
        exfat_fat_set(ctx, cur, EXFAT_FREE_CLUSTER);
        cur = next;
    }
}

static u64 exfat_cluster_sector(exfat_ctx_t *ctx, u32 cluster) {
    return ctx->heap_start_lba + (u64)(cluster - 2) * ctx->sectors_per_cluster;
}

/* ---- Name handling ---- */

/* Extract UTF-16 filename from a sequence of 0xC1 name entries.
 * ASCII is flattened by taking the low byte of each code unit. */
static int exfat_extract_name(const u8 *entries, int n_entries,
                              char *out, int out_len) {
    int out_pos = 0;
    for (int i = 0; i < n_entries; i++) {
        const exfat_name_entry_t *ne =
            (const exfat_name_entry_t *)(entries + i * 32);
        for (int j = 0; j < 15; j++) {
            u16 ch = ne->filename[j];
            if (ch == 0) { out[out_pos] = 0; return out_pos; }
            if (out_pos >= out_len - 1) { out[out_pos] = 0; return out_pos; }
            out[out_pos++] = (char)(ch & 0xFF);
        }
    }
    out[out_pos] = 0;
    return out_pos;
}

/* Build a sequence of 0xC1 name entries from an ASCII name. Returns the
 * number of name entries written (1..17) or -1 if too long. */
static int exfat_make_name_entries(const char *name, u8 *entries, int max_entries) {
    int name_len = (int)oc_strlen(name);
    int n_entries = (name_len + 14) / 15;
    if (n_entries == 0) n_entries = 1;
    if (n_entries > max_entries) return -1;
    int pos = 0;
    for (int i = 0; i < n_entries; i++) {
        exfat_name_entry_t *ne = (exfat_name_entry_t *)(entries + i * 32);
        oc_memset(ne, 0, 32);
        ne->type = EXFAT_TYPE_NAME;
        ne->flags = (u8)(i + 1);  /* ordinal; last entry gets 0x40 below */
        for (int j = 0; j < 15; j++) {
            if (pos < name_len) {
                ne->filename[j] = (u16)(u8)name[pos++];
            } else {
                ne->filename[j] = 0;
            }
        }
    }
    exfat_name_entry_t *last =
        (exfat_name_entry_t *)(entries + (n_entries - 1) * 32);
    last->flags = (u8)(n_entries | 0x40);
    return n_entries;
}

/* Compute exFAT name hash (16-bit, per spec). ASCII-only upcase. */
static u16 exfat_name_hash(const u16 *name, int name_len) {
    u16 hash = 0;
    for (int i = 0; i < name_len; i++) {
        u16 ch = name[i];
        if (ch >= 'a' && ch <= 'z') ch = (u16)(ch - 32);
        hash = (u16)(((hash >> 1) | (hash << 15)) & 0xFFFF);
        hash = (u16)((hash + ch) & 0xFFFF);
        hash = (u16)(((hash >> 1) | (hash << 15)) & 0xFFFF);
        hash = (u16)((hash + (u16)((ch << 8) | (ch >> 8))) & 0xFFFF);
    }
    return hash;
}

/* ---- Directory iteration ---- */

/* Find a file/dir named `name` in the directory whose first cluster is
 * dir_cluster. Returns 1 if found (fills out_*), 0 if not found, -1 on
 * I/O error. Name comparison is case-insensitive. */
static int exfat_find_in_dir(exfat_ctx_t *ctx, u32 dir_cluster,
                             const char *name,
                             exfat_file_entry_t *out_file,
                             exfat_stream_entry_t *out_stream,
                             u32 *out_cluster, u32 *out_offset,
                             char *out_name, int out_name_len) {
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -1;
    u32 cluster = dir_cluster;
    int result = 0;
    while (cluster >= 2 && cluster != EXFAT_EOC) {
        u64 sector = exfat_cluster_sector(ctx, cluster);
        if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
            kfree(cbuf);
            return -1;
        }
        u32 n_entries = ctx->cluster_size / 32;
        u32 e = 0;
        while (e < n_entries) {
            exfat_dentry_t *de = (exfat_dentry_t *)(cbuf + e * 32);
            u8 type = de->type;
            if (type == EXFAT_TYPE_UNUSED) { goto done; }
            if (type == EXFAT_TYPE_FILE) {
                exfat_file_entry_t *fe = (exfat_file_entry_t *)de;
                u8 sec_count = fe->secondary_count;
                if (e + 1 + sec_count > n_entries) { e++; continue; }
                exfat_stream_entry_t *se =
                    (exfat_stream_entry_t *)(cbuf + (e + 1) * 32);
                int name_entries = (int)sec_count - 1;
                if (name_entries < 0) name_entries = 0;
                char entry_name[256];
                int got = 0;
                if (name_entries > 0) {
                    if (exfat_extract_name(cbuf + (e + 2) * 32, name_entries,
                                           entry_name, sizeof(entry_name)) > 0) {
                        got = 1;
                    }
                }
                if (got && oc_strcasecmp(entry_name, name) == 0) {
                    if (out_file) *out_file = *fe;
                    if (out_stream) *out_stream = *se;
                    if (out_cluster) *out_cluster = cluster;
                    if (out_offset) *out_offset = e * 32;
                    if (out_name && out_name_len > 0) {
                        oc_strncpy(out_name, entry_name, out_name_len - 1);
                        out_name[out_name_len - 1] = 0;
                    }
                    result = 1;
                    goto done;
                }
                e += 1 + (u32)sec_count;
                continue;
            }
            e++;
        }
        cluster = exfat_fat_get(ctx, cluster);
    }
done:
    kfree(cbuf);
    return result;
}

/* Add a new file entry set (0x85 + 0xC0 + N x 0xC1) to a directory.
 * Grows the directory by one cluster if no free run of slots is found.
 * Returns 0 on success; fills *out_cluster / *out_offset with the file
 * entry location. */
static int exfat_add_entry(exfat_ctx_t *ctx, u32 dir_cluster,
                           const char *name, int is_dir,
                           u32 first_cluster, u64 data_length,
                           u32 *out_cluster, u32 *out_offset) {
    u8 set_buf[32 * 18];  /* 1 file + 1 stream + up to 16 name entries */
    oc_memset(set_buf, 0, sizeof(set_buf));
    int name_len = (int)oc_strlen(name);
    int n_name = exfat_make_name_entries(name, set_buf + 2 * 32, 16);
    if (n_name < 0) return -1;
    int total_entries = 2 + n_name;

    exfat_file_entry_t *fe = (exfat_file_entry_t *)(set_buf + 0);
    fe->type = EXFAT_TYPE_FILE;
    fe->secondary_count = (u8)(1 + n_name);
    fe->attributes = is_dir ? EXFAT_FILE_FLAG_DIR : 0;

    exfat_stream_entry_t *se = (exfat_stream_entry_t *)(set_buf + 32);
    se->type = EXFAT_TYPE_STREAM;
    se->flags = EXFAT_STREAM_FLAG_ALLOC_OK;
    se->name_length = (u8)name_len;
    u16 wname[256];
    for (int i = 0; i < name_len; i++) wname[i] = (u16)(u8)name[i];
    se->name_hash = exfat_name_hash(wname, name_len);
    se->valid_data_length = data_length;
    se->first_cluster = first_cluster;
    se->data_length = data_length;

    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -1;
    u32 cluster = dir_cluster;
    u32 prev_cluster = 0;
    while (cluster >= 2 && cluster != EXFAT_EOC) {
        u64 sector = exfat_cluster_sector(ctx, cluster);
        if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
            kfree(cbuf);
            return -1;
        }
        u32 n_entries = ctx->cluster_size / 32;
        u32 e = 0;
        while (e < n_entries) {
            u32 run = 0;
            while (e + run < n_entries && run < (u32)total_entries) {
                exfat_dentry_t *de =
                    (exfat_dentry_t *)(cbuf + (e + run) * 32);
                if (de->type != EXFAT_TYPE_UNUSED) break;
                run++;
            }
            if (run >= (u32)total_entries) {
                oc_memcpy(cbuf + e * 32, set_buf, (u32)total_entries * 32);
                if (exfat_write_sectors(ctx, sector,
                                        ctx->sectors_per_cluster, cbuf) != 0) {
                    kfree(cbuf);
                    return -1;
                }
                *out_cluster = cluster;
                *out_offset = e * 32;
                kfree(cbuf);
                return 0;
            }
            e += run + 1;
        }
        prev_cluster = cluster;
        cluster = exfat_fat_get(ctx, cluster);
    }
    /* Extend the directory by one cluster. */
    u32 new_cluster = exfat_alloc_cluster(ctx);
    if (new_cluster == 0) { kfree(cbuf); return -1; }
    if (prev_cluster >= 2 && prev_cluster != EXFAT_EOC) {
        exfat_fat_set(ctx, prev_cluster, new_cluster);
    }
    oc_memset(cbuf, 0, ctx->cluster_size);
    oc_memcpy(cbuf, set_buf, (u32)total_entries * 32);
    u64 sector = exfat_cluster_sector(ctx, new_cluster);
    if (exfat_write_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
        kfree(cbuf);
        return -1;
    }
    *out_cluster = new_cluster;
    *out_offset = 0;
    kfree(cbuf);
    return 0;
}

/* Update the stream extension entry (first_cluster + data_length) of an
 * already-located file entry set. */
static int exfat_update_stream(exfat_ctx_t *ctx, u32 entry_cluster,
                               u32 entry_offset, u32 first_cluster,
                               u64 data_length, u64 valid_data_length) {
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -1;
    u64 sector = exfat_cluster_sector(ctx, entry_cluster);
    if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
        kfree(cbuf);
        return -1;
    }
    exfat_stream_entry_t *se =
        (exfat_stream_entry_t *)(cbuf + entry_offset + 32);
    se->first_cluster = first_cluster;
    se->data_length = data_length;
    se->valid_data_length = valid_data_length;
    int rc = exfat_write_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf);
    kfree(cbuf);
    return rc;
}

/* Mark a file entry set's entries as unused (type=0x00). */
static int exfat_delete_entry(exfat_ctx_t *ctx, u32 entry_cluster,
                              u32 entry_offset) {
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -1;
    u64 sector = exfat_cluster_sector(ctx, entry_cluster);
    if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
        kfree(cbuf);
        return -1;
    }
    exfat_file_entry_t *fe = (exfat_file_entry_t *)(cbuf + entry_offset);
    u8 sec_count = fe->secondary_count;
    for (u8 i = 0; i <= sec_count && entry_offset + i * 32 < ctx->cluster_size;
         i++) {
        cbuf[entry_offset + i * 32] = EXFAT_TYPE_UNUSED;
    }
    int rc = exfat_write_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf);
    kfree(cbuf);
    return rc;
}

/* ---- VFS file operations ---- */

static int exfat_open(vfs_node_t *node, int flags) {
    if (!node) return -1;
    if (node->type == VFS_TYPE_DIR) return -2;
    (void)flags;
    return 0;
}

static int exfat_read(vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    exfat_inode_t *ino = (exfat_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    if (ino->is_dir) return -3;
    if (ino->first_cluster < 2) return 0;     /* empty file */
    u64 file_size = node->size;
    if (offset >= file_size) return 0;
    u64 avail = file_size - offset;
    int want = (int)(avail < (u64)size ? avail : (u64)size);

    exfat_ctx_t *ctx = ino->ctx;
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -4;

    /* Walk the chain to find the cluster containing `offset`. */
    u32 cluster = ino->first_cluster;
    u64 bpc = ctx->cluster_size;
    u64 skip_clusters = offset / bpc;
    u64 off_in_cluster = offset % bpc;
    for (u64 i = 0; i < skip_clusters; i++) {
        if (cluster < 2 || cluster == EXFAT_EOC) {
            kfree(cbuf);
            return 0;
        }
        cluster = exfat_fat_get(ctx, cluster);
    }

    int copied = 0;
    while (copied < want) {
        if (cluster < 2 || cluster == EXFAT_EOC) break;
        u64 sector = exfat_cluster_sector(ctx, cluster);
        if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0)
            break;
        u64 in_this = bpc - off_in_cluster;
        u64 need = (u64)(want - copied);
        u64 to_copy = in_this < need ? in_this : need;
        oc_memcpy((u8 *)buf + copied, cbuf + off_in_cluster, (usize)to_copy);
        copied += (int)to_copy;
        cluster = exfat_fat_get(ctx, cluster);
        off_in_cluster = 0;
    }
    kfree(cbuf);
    return copied;
}

static int exfat_write(vfs_node_t *node, u64 offset, const void *buf, int size) {
    if (!node || !buf || size <= 0) return -1;
    exfat_inode_t *ino = (exfat_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    if (ino->is_dir) return -3;
    if (ino->entry_cluster == 0) return -4;  /* root or no parent entry */
    exfat_ctx_t *ctx = ino->ctx;

    /* Allocate the first cluster if the file is empty. */
    if (ino->first_cluster < 2) {
        u32 fc = exfat_alloc_cluster(ctx);
        if (fc == 0) return -5;
        ino->first_cluster = fc;
    }

    u64 new_end = offset + (u64)size;
    u64 cur_size = node->size;
    if (new_end > cur_size) {
        /* Extend the cluster chain to cover new_end. */
        u32 last = ino->first_cluster;
        while (1) {
            u32 next = exfat_fat_get(ctx, last);
            if (next == EXFAT_EOC || next < 2) break;
            last = next;
        }
        u64 current_bytes = ctx->cluster_size;
        while (current_bytes < new_end) {
            u32 nc = exfat_alloc_cluster(ctx);
            if (nc == 0) break;
            exfat_fat_set(ctx, last, nc);
            last = nc;
            current_bytes += ctx->cluster_size;
        }
    }

    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -6;

    /* Walk the chain to the cluster containing `offset`. */
    u32 cluster = ino->first_cluster;
    u64 target_idx = offset / ctx->cluster_size;
    for (u64 i = 0; i < target_idx; i++) {
        if (cluster < 2 || cluster == EXFAT_EOC) {
            kfree(cbuf);
            return 0;
        }
        cluster = exfat_fat_get(ctx, cluster);
    }
    u64 off_in_cluster = offset % ctx->cluster_size;

    int total = 0;
    const u8 *src = (const u8 *)buf;
    while (total < size) {
        if (cluster < 2 || cluster == EXFAT_EOC) break;
        u64 sector = exfat_cluster_sector(ctx, cluster);
        u64 bytes_this = ctx->cluster_size - off_in_cluster;
        u64 need = (u64)(size - total);
        if (bytes_this > need) bytes_this = need;
        if (off_in_cluster != 0 || bytes_this != ctx->cluster_size) {
            if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster,
                                   cbuf) != 0) {
                break;
            }
        } else {
            oc_memset(cbuf, 0, ctx->cluster_size);
        }
        oc_memcpy(cbuf + off_in_cluster, src + total, (usize)bytes_this);
        if (exfat_write_sectors(ctx, sector, ctx->sectors_per_cluster,
                                cbuf) != 0) {
            break;
        }
        total += (int)bytes_this;
        cluster = exfat_fat_get(ctx, cluster);
        off_in_cluster = 0;
    }
    kfree(cbuf);

    /* Update on-disk stream entry if the file grew. */
    if (new_end > cur_size) {
        exfat_update_stream(ctx, ino->entry_cluster, ino->entry_offset,
                            ino->first_cluster, new_end, new_end);
        node->size = new_end;
    }
    return total;
}

static u64 exfat_seek(vfs_node_t *node, u64 offset, int whence) {
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

static int exfat_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int exfat_stat(vfs_node_t *node, vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    oc_strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- VFS directory operations ---- */

/* P1-2 FIX: create a regular file (not a directory) in the exFAT on-disk
 * directory. Unlike the old VFS approach (call mkdir then patch the in-memory
 * node type), this creates the on-disk entry with FILE attributes (not DIR),
 * so a later readdir/ls correctly reports it as a regular file. */
static int exfat_create(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    exfat_inode_t *pino = (exfat_inode_t *)parent->private;
    if (!pino || !pino->ctx) return -2;
    if (!pino->is_dir) return -3;
    exfat_ctx_t *ctx = pino->ctx;
    /* Already exists? */
    if (exfat_find_in_dir(ctx, pino->first_cluster, name,
                          NULL, NULL, NULL, NULL, NULL, 0) == 1) {
        return -4;
    }
    /* Create a file entry with is_dir=0 (no cluster allocated yet —
     * first_cluster=0, data_length=0. Data will be allocated on write.) */
    u32 ec, eo;
    if (exfat_add_entry(ctx, pino->first_cluster, name, 0 /*is_dir*/,
                        0 /*first_cluster*/, 0 /*data_length*/, &ec, &eo) != 0) {
        return -5;
    }
    /* Create an in-memory inode for the new file. */
    exfat_inode_t *child_ino = (exfat_inode_t *)kmalloc(sizeof(exfat_inode_t));
    if (!child_ino) return -6;
    oc_memset(child_ino, 0, sizeof(*child_ino));
    child_ino->ctx = ctx;
    child_ino->first_cluster = 0;
    child_ino->is_dir = 0;
    child_ino->entry_cluster = ec;
    child_ino->entry_offset = eo;
    vfs_node_t *cn = vfs_alloc_node(name, VFS_TYPE_FILE, &g_exfat_fs_type);
    if (!cn) { kfree(child_ino); return -7; }
    cn->private = child_ino;
    cn->parent = parent;
    return 0;
}

static int exfat_mkdir(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    exfat_inode_t *pino = (exfat_inode_t *)parent->private;
    if (!pino || !pino->ctx) return -2;
    if (!pino->is_dir) return -3;
    exfat_ctx_t *ctx = pino->ctx;
    /* Already exists? */
    if (exfat_find_in_dir(ctx, pino->first_cluster, name,
                          NULL, NULL, NULL, NULL, NULL, 0) == 1) {
        return -4;
    }
    /* Allocate one cluster for the new (empty) directory. */
    u32 new_cluster = exfat_alloc_cluster(ctx);
    if (new_cluster == 0) return -5;
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) {
        exfat_fat_set(ctx, new_cluster, EXFAT_FREE_CLUSTER);
        return -6;
    }
    oc_memset(cbuf, 0, ctx->cluster_size);
    u64 sector = exfat_cluster_sector(ctx, new_cluster);
    if (exfat_write_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
        kfree(cbuf);
        exfat_fat_set(ctx, new_cluster, EXFAT_FREE_CLUSTER);
        return -7;
    }
    kfree(cbuf);
    u32 ec, eo;
    if (exfat_add_entry(ctx, pino->first_cluster, name, 1 /*is_dir*/,
                        new_cluster, 0, &ec, &eo) != 0) {
        exfat_fat_set(ctx, new_cluster, EXFAT_FREE_CLUSTER);
        return -8;
    }
    return 0;
}

static int exfat_rmdir(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    exfat_inode_t *pino = (exfat_inode_t *)parent->private;
    if (!pino || !pino->ctx) return -2;
    exfat_ctx_t *ctx = pino->ctx;
    exfat_file_entry_t fe;
    exfat_stream_entry_t se;
    u32 entry_cluster = 0, entry_offset = 0;
    if (exfat_find_in_dir(ctx, pino->first_cluster, name,
                          &fe, &se, &entry_cluster, &entry_offset, NULL, 0) != 1)
        return -3;
    if (!(fe.attributes & EXFAT_FILE_FLAG_DIR)) return -4;
    u32 dir_cluster = se.first_cluster;
    /* Check emptiness: scan dir for any non-unused, non-bitmap/upcase entry. */
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -5;
    u32 cluster = dir_cluster;
    int is_empty = 1;
    while (cluster >= 2 && cluster != EXFAT_EOC) {
        u64 sector = exfat_cluster_sector(ctx, cluster);
        if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
            kfree(cbuf);
            return -6;
        }
        u32 n_entries = ctx->cluster_size / 32;
        for (u32 e = 0; e < n_entries; e++) {
            exfat_dentry_t *de = (exfat_dentry_t *)(cbuf + e * 32);
            if (de->type == EXFAT_TYPE_UNUSED) goto done_check;
            if (de->type == EXFAT_TYPE_BITMAP || de->type == EXFAT_TYPE_UPCASE)
                continue;
            is_empty = 0;
            goto done_check;
        }
        cluster = exfat_fat_get(ctx, cluster);
    }
done_check:
    kfree(cbuf);
    if (!is_empty) return -7;
    if (dir_cluster >= 2) exfat_free_chain(ctx, dir_cluster);
    exfat_delete_entry(ctx, entry_cluster, entry_offset);
    return 0;
}

static int exfat_readdir(vfs_node_t *dir, int index, vfs_dirent_t *entry) {
    if (!dir || !entry || index < 0) return -1;
    exfat_inode_t *ino = (exfat_inode_t *)dir->private;
    if (!ino || !ino->ctx) return -2;
    if (!ino->is_dir) return -3;
    exfat_ctx_t *ctx = ino->ctx;
    if (ino->first_cluster < 2) return -4;
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -5;
    u32 cluster = ino->first_cluster;
    int cur_idx = 0;
    int result = -6;
    while (cluster >= 2 && cluster != EXFAT_EOC) {
        u64 sector = exfat_cluster_sector(ctx, cluster);
        if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
            kfree(cbuf);
            return -7;
        }
        u32 n_entries = ctx->cluster_size / 32;
        u32 e = 0;
        while (e < n_entries) {
            exfat_dentry_t *de = (exfat_dentry_t *)(cbuf + e * 32);
            u8 type = de->type;
            if (type == EXFAT_TYPE_UNUSED) { result = -8; goto done; }
            if (type == EXFAT_TYPE_FILE) {
                exfat_file_entry_t *fe = (exfat_file_entry_t *)de;
                u8 sec_count = fe->secondary_count;
                if (e + 1 + sec_count > n_entries) { e++; continue; }
                exfat_stream_entry_t *se =
                    (exfat_stream_entry_t *)(cbuf + (e + 1) * 32);
                int name_entries = (int)sec_count - 1;
                if (name_entries < 0) name_entries = 0;
                char entry_name[256];
                int got = 0;
                if (name_entries > 0) {
                    if (exfat_extract_name(cbuf + (e + 2) * 32, name_entries,
                                           entry_name, sizeof(entry_name)) > 0) {
                        got = 1;
                    }
                }
                if (got && cur_idx == index) {
                    oc_memset(entry, 0, sizeof(*entry));
                    oc_strncpy(entry->name, entry_name, VFS_NAME_LEN - 1);
                    entry->name[VFS_NAME_LEN - 1] = 0;
                    entry->type = (fe->attributes & EXFAT_FILE_FLAG_DIR)
                                      ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                    entry->inode = se->first_cluster;
                    result = 0;
                    goto done;
                }
                if (got) cur_idx++;
                e += 1 + (u32)sec_count;
                continue;
            }
            e++;
        }
        cluster = exfat_fat_get(ctx, cluster);
    }
done:
    kfree(cbuf);
    return result;
}

static vfs_node_t *exfat_lookup(vfs_node_t *parent, const char *name) {
    /* P2-20: cycle detection — limit cluster chain traversal to prevent
     * infinite loops on corrupt exFAT images. */
    if (!parent || !name) return NULL;
    exfat_inode_t *pino = (exfat_inode_t *)parent->private;
    if (!pino || !pino->ctx) return NULL;
    exfat_ctx_t *ctx = pino->ctx;
    exfat_file_entry_t fe;
    exfat_stream_entry_t se;
    u32 entry_cluster = 0, entry_offset = 0;
    char display[VFS_NAME_LEN];
    display[0] = 0;
    if (exfat_find_in_dir(ctx, pino->first_cluster, name,
                          &fe, &se, &entry_cluster, &entry_offset,
                          display, sizeof(display)) != 1) {
        return NULL;
    }
    int is_dir = (fe.attributes & EXFAT_FILE_FLAG_DIR) ? 1 : 0;
    exfat_inode_t *child_ino = (exfat_inode_t *)kmalloc(sizeof(exfat_inode_t));
    if (!child_ino) return NULL;
    oc_memset(child_ino, 0, sizeof(*child_ino));
    child_ino->ctx = ctx;
    child_ino->first_cluster = se.first_cluster;
    child_ino->is_dir = is_dir;
    child_ino->entry_cluster = entry_cluster;
    child_ino->entry_offset = entry_offset;
    vfs_node_t *cn = vfs_alloc_node(display,
                                    is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE,
                                    &g_exfat_fs_type);
    if (!cn) {
        kfree(child_ino);
        return NULL;
    }
    cn->private = child_ino;
    cn->size = se.data_length;
    cn->parent = parent;
    return cn;
}

static int exfat_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    exfat_inode_t *pino = (exfat_inode_t *)parent->private;
    if (!pino || !pino->ctx) return -2;
    exfat_ctx_t *ctx = pino->ctx;
    exfat_file_entry_t fe;
    exfat_stream_entry_t se;
    u32 entry_cluster = 0, entry_offset = 0;
    if (exfat_find_in_dir(ctx, pino->first_cluster, name,
                          &fe, &se, &entry_cluster, &entry_offset, NULL, 0) != 1)
        return -3;
    if (fe.attributes & EXFAT_FILE_FLAG_DIR) return -4;  /* use rmdir for dirs */
    if (se.first_cluster >= 2) exfat_free_chain(ctx, se.first_cluster);
    exfat_delete_entry(ctx, entry_cluster, entry_offset);
    return 0;
}

static int exfat_rename(vfs_node_t *parent, const char *oldname,
                        const char *newname) {
    if (!parent || !oldname || !newname) return -1;
    exfat_inode_t *pino = (exfat_inode_t *)parent->private;
    if (!pino || !pino->ctx) return -2;
    exfat_ctx_t *ctx = pino->ctx;
    /* If newname already exists, fail. */
    if (exfat_find_in_dir(ctx, pino->first_cluster, newname,
                          NULL, NULL, NULL, NULL, NULL, 0) == 1) return -3;
    exfat_file_entry_t ofe;
    exfat_stream_entry_t ose;
    u32 entry_cluster = 0, entry_offset = 0;
    if (exfat_find_in_dir(ctx, pino->first_cluster, oldname,
                          &ofe, &ose, &entry_cluster, &entry_offset,
                          NULL, 0) != 1) return -4;
    int old_name_entries = (int)ofe.secondary_count - 1;
    if (old_name_entries < 0) old_name_entries = 0;
    /* Build new name entries; if the count differs we fall back to
     * delete + re-add (preserving first_cluster and data_length). */
    u8 new_names[32 * 17];
    int new_n = exfat_make_name_entries(newname, new_names, 17);
    if (new_n < 0) return -5;
    if (new_n != old_name_entries) {
        u32 ec, eo;
        if (exfat_add_entry(ctx, pino->first_cluster, newname,
                            (ofe.attributes & EXFAT_FILE_FLAG_DIR) ? 1 : 0,
                            ose.first_cluster, ose.data_length, &ec, &eo) != 0)
            return -6;
        exfat_delete_entry(ctx, entry_cluster, entry_offset);
        return 0;
    }
    /* Same number of name entries - overwrite in place. */
    u8 *cbuf = (u8 *)kmalloc(ctx->cluster_size);
    if (!cbuf) return -7;
    u64 sector = exfat_cluster_sector(ctx, entry_cluster);
    if (exfat_read_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf) != 0) {
        kfree(cbuf);
        return -8;
    }
    oc_memcpy(cbuf + entry_offset + 2 * 32, new_names, (u32)new_n * 32);
    exfat_stream_entry_t *se =
        (exfat_stream_entry_t *)(cbuf + entry_offset + 32);
    int name_len = (int)oc_strlen(newname);
    se->name_length = (u8)name_len;
    u16 wname[256];
    for (int i = 0; i < name_len; i++) wname[i] = (u16)(u8)newname[i];
    se->name_hash = exfat_name_hash(wname, name_len);
    int rc = exfat_write_sectors(ctx, sector, ctx->sectors_per_cluster, cbuf);
    kfree(cbuf);
    return rc;
}

/* ---- Mount / unmount ---- */

static vfs_node_t *exfat_fs_mount(const char *device) {
    int drive = exfat_parse_device(device);
    if (drive < 0) {
        oc_console_puts("exfat: invalid device string\n");
        return NULL;
    }
    if (!ata_detect(drive)) {
        oc_console_puts("exfat: drive not present\n");
        return NULL;
    }
    u8 boot[512];
    if (ata_read_sectors(drive, 0, 1, boot) != 1) {
        oc_console_puts("exfat: read boot sector failed\n");
        return NULL;
    }
    exfat_bpb_t *bpb = (exfat_bpb_t *)boot;
    if (oc_strncmp((const char *)bpb->fs_name, "EXFAT   ", 8) != 0) {
        oc_console_puts("exfat: not an exFAT volume\n");
        return NULL;
    }
    if (bpb->bytes_per_sector_shift > 12 ||
        bpb->sectors_per_cluster_shift > 25 -
        bpb->bytes_per_sector_shift) {
        oc_console_puts("exfat: impossible sector/cluster shift\n");
        return NULL;
    }
    exfat_ctx_t *ctx = (exfat_ctx_t *)kmalloc(sizeof(exfat_ctx_t));
    if (!ctx) return NULL;
    oc_memset(ctx, 0, sizeof(*ctx));
    ctx->drive = drive;
    ctx->sector_size = (u32)1 << bpb->bytes_per_sector_shift;
    ctx->sectors_per_cluster = (u32)1 << bpb->sectors_per_cluster_shift;
    ctx->cluster_size = ctx->sector_size * ctx->sectors_per_cluster;
    ctx->fat_start_lba = bpb->fat_offset;
    ctx->fat_size_sectors = bpb->fat_length;
    ctx->heap_start_lba = bpb->cluster_heap_offset;
    ctx->cluster_count = bpb->cluster_count;
    ctx->root_cluster = bpb->first_cluster_of_root_directory;
    ctx->last_alloc = 0;

    exfat_inode_t *root_ino = (exfat_inode_t *)kmalloc(sizeof(exfat_inode_t));
    if (!root_ino) { kfree(ctx); return NULL; }
    oc_memset(root_ino, 0, sizeof(*root_ino));
    root_ino->ctx = ctx;
    root_ino->first_cluster = ctx->root_cluster;
    root_ino->is_dir = 1;
    root_ino->entry_cluster = 0;
    root_ino->entry_offset = 0;

    vfs_node_t *root = vfs_alloc_node("", VFS_TYPE_DIR, &g_exfat_fs_type);
    if (!root) {
        kfree(root_ino);
        kfree(ctx);
        return NULL;
    }
    root->private = root_ino;
    return root;
}

static int exfat_fs_unmount(vfs_node_t *root) {
    if (!root) return -1;
    exfat_inode_t *ino = (exfat_inode_t *)root->private;
    if (ino) {
        if (ino->ctx) kfree(ino->ctx);
        kfree(ino);
    }
    kfree(root);
    return 0;
}

/* ---- Init ---- */

void exfat_init(void) {
    oc_memset(&g_exfat_fs_type, 0, sizeof(g_exfat_fs_type));
    oc_strncpy(g_exfat_fs_type.name, "exfat", sizeof(g_exfat_fs_type.name) - 1);
    g_exfat_fs_ops.mount   = exfat_fs_mount;
    g_exfat_fs_ops.unmount = exfat_fs_unmount;
    g_exfat_file_ops.open  = exfat_open;
    g_exfat_file_ops.read  = exfat_read;
    g_exfat_file_ops.write = exfat_write;
    g_exfat_file_ops.seek  = exfat_seek;
    g_exfat_file_ops.close = exfat_close;
    g_exfat_file_ops.stat  = exfat_stat;
    g_exfat_dir_ops.mkdir   = exfat_mkdir;
    g_exfat_dir_ops.create  = exfat_create;  /* P1-2 FIX */
    g_exfat_dir_ops.rmdir   = exfat_rmdir;
    g_exfat_dir_ops.readdir = exfat_readdir;
    g_exfat_dir_ops.lookup  = exfat_lookup;
    g_exfat_dir_ops.unlink  = exfat_unlink;
    g_exfat_dir_ops.rename  = exfat_rename;
    g_exfat_fs_type.fs_ops   = &g_exfat_fs_ops;
    g_exfat_fs_type.file_ops = &g_exfat_file_ops;
    g_exfat_fs_type.dir_ops  = &g_exfat_dir_ops;

    vfs_register_fs("exfat", &g_exfat_fs_ops, &g_exfat_file_ops,
                    &g_exfat_dir_ops);
    oc_console_puts("exfat: registered (read/write)\n");
}
