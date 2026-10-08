/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/fat32.c
 * Purpose: FAT32 file system implementation (read-focused).
 *
 * Layout (LBA28 sectors, 512 bytes each):
 *   [reserved region: boot sector + (reserved_sectors-1) more]
 *   [FAT #0: fat_size_sectors sectors]
 *   [FAT #1: fat_size_sectors sectors]
 *   [data region: starts at data_start_lba, cluster 2 is the first one]
 *
 * BUG-0185 (A12-020): the FAT copy that gets cached and updated is the
 * ACTIVE one (BPB_ExtFlags, fs.doc 2.1.4 "FAT32 Extended BPB"); while
 * mirroring is enabled (ext_flags bit 7 = 0) every FAT-sector write is
 * replayed to every other copy, so FAT #1 is no longer left stale.
 *
 * FAT32 entries are 32 bits wide; the low 28 bits are the next-cluster
 * pointer. End-of-chain markers are 0x0FFFFFF8..0x0FFFFFFF. Free clusters
 * are 0x00000000.
 *
 * Directory entries are 32 bytes each. We support the classic 8.3 format
 * and a minimal LFN reader (collect LFN slots, assemble into a long name).
 */
#include "fs_fat32.h"
#include "fs_vfs.h"
#include "driver_block_blk.h"
#include "driver_block_ata.h"
#include "screen_console.h"
#include "mem_heap.h"
#include "lib_string.h"

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
} fs_fat32_bpb_t;

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
} fs_fat32_dirent_t;

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
} fs_fat32_lfn_t;

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
    int   dev_idx;               /* WP-10a: blk-layer device index (any driver:
                                    IDE, AHCI, virtio-blk, NVMe, ...) */
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
    u32   next_free_hint;        /* BUG-0186: FSINFO next-free hint */
    u32   free_count_hint;       /* BUG-0186: FSINFO free-cluster count */
    u32   fsinfo_lba;            /* 0 = no FSINFO (pre-0.4 volumes) */
    u8   *fsinfo_sector;         /*512B scratch for FSINFO updates*/
    u32   active_fat;            /* BUG-0185: FAT index from BPB_ExtFlags */
    u8    fat_mirror;            /* BUG-0185: 1 = keep mirror FATs in sync */
} fs_fat32_ctx_t;

/* Per-node private data. */
typedef struct {
    fs_fat32_ctx_t *ctx;
    u32  start_cluster;          /* 0 for empty file */
    u64  size;                   /* bytes for files, 0 for dirs */
    int  is_dir;
} fs_fat32_inode_t;

static fs_vfs_fs_type_t  g_fat32_fs_type;
static fs_vfs_fs_ops_t   g_fat32_fs_ops;
static fs_vfs_file_ops_t g_fat32_file_ops;
static fs_vfs_dir_ops_t  g_fat32_dir_ops;

/* Stats (last mounted ctx; for fs_fat32_get_stats). */
static fs_fat32_ctx_t *g_last_ctx = NULL;

/* ---- Helpers ---- */

/* Convert an 8.3 name (11 bytes, space-padded) to a displayable string.
 * "KERNEL  ELF" -> "KERNEL.ELF" */
static void fs_fat32_format_short_name(const u8 *raw, char *out) {
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

/* Parse a device string into a blk-layer device index.
 * WP-10a: the primary form is any registered block-device name ("hda",
 * "sda", "vda", "nvme0", ...) looked up through driver_block_find_device(), so
 * FAT32 volumes can live on ANY storage driver.  Legacy forms are kept
 * for compatibility: "ata0".."ata3" / "ata" / "0".."3" / "hda".."hdd"
 * map to the corresponding ATA block device. Returns -1 on bad input. */
static int fs_fat32_parse_device(const char *device) {
    if (!device || !device[0]) return -1;
    /* Any registered block-device name (sda, nvme0, vda, hda, ...). */
    int idx = driver_block_find_device(device);
    if (idx >= 0) return idx;
    /* Legacy: "ataN" / "ata" / "N" -> hdX name. */
    char hd[6] = "hd?";
    if (strcmp(device, "ata") == 0) { hd[2] = 'a'; return driver_block_find_device(hd); }
    if (strncmp(device, "ata", 3) == 0) {
        char c = device[3];
        if (c >= '0' && c <= '3' && device[4] == 0) {
            hd[2] = (char)('a' + (c - '0'));
            return driver_block_find_device(hd);
        }
        return -1;
    }
    if (device[0] >= '0' && device[0] <= '3' && device[1] == 0) {
        hd[2] = (char)('a' + (device[0] - '0'));
        return driver_block_find_device(hd);
    }
    return -1;
}

/* Read a whole cluster into buf (must be bytes_per_cluster bytes). */
/* P0fix1 BUG-0004 (A12-004): cluster numbers come off disk (FAT chains,
 * directory entries). Anything >= total_clusters + 2 maps to an LBA outside
 * the volume and read/write would corrupt the neighbouring area. */
static int fs_fat32_cluster_valid(const fs_fat32_ctx_t *ctx, u32 cluster) {
    if (cluster < 2) return 0;
    if (cluster >= (u64)ctx->total_clusters + 2) return 0;   /* outside the volume */
    return 1;
}

static int fs_fat32_read_cluster(fs_fat32_ctx_t *ctx, u32 cluster, u8 *buf) {
    if (!fs_fat32_cluster_valid(ctx, cluster)) return -1;
    u64 lba = (u64)ctx->data_start_lba + (u64)(cluster - 2) * (u64)ctx->sectors_per_cluster;
    return driver_block_read_sectors_raw(ctx->dev_idx, lba,
                                (u32)ctx->sectors_per_cluster, buf);
}

/* Return the next cluster in the chain, or 0 (== EOC) on end. */
static u32 fs_fat32_next_cluster(fs_fat32_ctx_t *ctx, u32 cluster) {
    if (!ctx->fat_cache) return 0;
    u64 off = (u64)cluster * 4;
    if (off + 4 > ctx->fat_cache_size) return 0;
    u32 v;
    memcpy(&v, ctx->fat_cache + off, 4);
    return v & 0x0FFFFFFFu;
}

/* ---- FAT table / cluster helpers (write support) ---- */

/* Set a FAT entry's low 28 bits to `value`. Updates the in-memory cache and
 * writes the containing sector back to disk. The top 4 reserved bits of the
 * existing entry are preserved. Returns 0 on success, negative on error. */
static int fs_fat32_set_fat_entry(fs_fat32_ctx_t *ctx, u32 cluster, u32 value) {
    if (!ctx->fat_cache || cluster < 2) return -1;
    u64 off = (u64)cluster * 4;
    if (off + 4 > ctx->fat_cache_size) return -2;
    u32 oldv;
    memcpy(&oldv, ctx->fat_cache + off, 4);
    u32 newv = (oldv & 0xF0000000u) | (value & 0x0FFFFFFFu);
    memcpy(ctx->fat_cache + off, &newv, 4);
    u64 sector_offset = off / ctx->bytes_per_sector;
    int rc = driver_block_write_sectors_raw(ctx->dev_idx,
                               (u64)ctx->fat_start_lba + sector_offset, 1,
                               ctx->fat_cache + sector_offset * ctx->bytes_per_sector);
    if (rc != 0) return -3;
    /* BUG-0185 FIX (A12-020): keep the mirror FATs in sync. The old
     * comment said "we only use FAT #0" and never touched the mirror,
     * so a second OS honouring the dual-FAT layout read a stale table
     * (fsck-style tools flag it; some drivers repair from FAT #1). Per
     * fs.doc 2.1.4 (BPB_ExtFlags), when mirroring is ENABLED all FAT
     * copies must be updated on every change; when it is DISABLED only
     * the active FAT is maintained (the loop below then writes nothing).
     * num_fats is validated <= 4 at mount, so the loop is bounded. */
    if (ctx->fat_mirror && ctx->num_fats >= 2) {
        for (u32 f = 0; f < ctx->num_fats; f++) {
            if (f == ctx->active_fat) continue;
            u64 fat_base = (u64)ctx->reserved_sectors +
                           (u64)f * ctx->fat_size_sectors;
            (void)driver_block_write_sectors_raw(ctx->dev_idx,
                fat_base + sector_offset, 1,
                ctx->fat_cache + sector_offset * ctx->bytes_per_sector);
        }
    }
    return 0;
}

static void fs_fat32_fsinfo_write(fs_fat32_ctx_t *ctx);   /* BUG-0186 fwd */

/* Allocate a free cluster from the FAT. Marks it as EOC (0x0FFFFFFF) and
 * returns the cluster number. Returns 0 on failure (no free cluster or I/O
 * error). */
static u32 fs_fat32_alloc_cluster(fs_fat32_ctx_t *ctx) {
    if (!ctx->fat_cache) return 0;
    u64 entries = ctx->fat_cache_size / 4;
    u64 limit = (u64)ctx->total_clusters + 2;
    if (limit > entries) limit = entries;
    /* BUG-0186 FIX (A12-021): start the scan at the FSINFO next-free
     * hint instead of always restarting at cluster 2 (O(volume) scan on
     * every allocation for fragmented volumes), wrapping to 2 when the
     * tail is exhausted. */
    u32 start = ctx->next_free_hint;
    if (start < 2 || start >= limit) start = 2;
    for (u64 pass = 0; pass < 2; pass++) {
        u64 from = (pass == 0) ? start : 2;
        u64 to   = (pass == 0) ? limit : start;
        for (u64 i = from; i < to; i++) {
            u32 v;
            memcpy(&v, ctx->fat_cache + i * 4, 4);
            if ((v & 0x0FFFFFFFu) == 0) {
                if (fs_fat32_set_fat_entry(ctx, (u32)i, FAT_EOC_END) < 0) return 0;
                ctx->next_free_hint = (u32)i + 1;
                /* 0xFFFFFFFF = "unknown" per the FSInfo spec - leave it. */
                if (ctx->free_count_hint != 0xFFFFFFFFu && ctx->free_count_hint > 0)
                    ctx->free_count_hint--;
                fs_fat32_fsinfo_write(ctx);
                return (u32)i;
            }
        }
    }
    return 0;
}

/* BUG-0186 FIX (A12-021): persist the FSINFO free-count / next-free
 * hints. FSINFO fields are advisory per the spec (7.1), so a stale value
 * is harmless - but keeping them current means other OSes' free-space
 * reporting and our own scan both stay fast and consistent. */
static void fs_fat32_fsinfo_write(fs_fat32_ctx_t *ctx) {
    if (!ctx->fsinfo_lba || !ctx->fsinfo_sector) return;
    memcpy(ctx->fsinfo_sector + 488, &ctx->free_count_hint, 4);
    memcpy(ctx->fsinfo_sector + 492, &ctx->next_free_hint, 4);
    (void)driver_block_write_sectors_raw(ctx->dev_idx, ctx->fsinfo_lba, 1,
                               ctx->fsinfo_sector);
}

/* Link `new_cluster` after `last_cluster` in the FAT chain. */
static int fs_fat32_append_cluster(fs_fat32_ctx_t *ctx, u32 last_cluster, u32 new_cluster) {
    return fs_fat32_set_fat_entry(ctx, last_cluster, new_cluster);
}

/* Free every cluster in the chain starting at `start_cluster`. */
static int fs_fat32_free_cluster_chain(fs_fat32_ctx_t *ctx, u32 start_cluster) {
    u32 cluster = start_cluster;
    u32 oc_hops = 0;   /* BUG-0058: bound the walk (corrupt chain = cycle) */
    u32 freed = 0;
    while (cluster >= 2 && cluster < FAT_EOC && oc_hops++ < ctx->total_clusters + 2u) {
        u32 next = fs_fat32_next_cluster(ctx, cluster);
        if (fs_fat32_set_fat_entry(ctx, cluster, 0x00000000u) < 0) return -1;
        freed++;
        cluster = next;
    }
    /* BUG-0186 FIX (A12-021): the FSINFO hints must be maintained on the
     * free path too (fs.doc "FSInfo Sector": free count at offset 488,
     * next-free at 492, both advisory). The released run becomes the
     * best next-free candidate and the free count grows accordingly. */
    if (freed > 0) {
        if (ctx->free_count_hint != 0xFFFFFFFFu)
            ctx->free_count_hint += freed;
        if (start_cluster >= 2 &&
            (ctx->next_free_hint < 2 || start_cluster < ctx->next_free_hint)) {
            ctx->next_free_hint = start_cluster;
        }
        fs_fat32_fsinfo_write(ctx);
    }
    return 0;
}

/* Write a full cluster (bytes_per_cluster bytes) from `buf` to disk. */
static int fs_fat32_write_cluster(fs_fat32_ctx_t *ctx, u32 cluster, const u8 *buf) {
    if (!fs_fat32_cluster_valid(ctx, cluster)) return -1;
    u64 lba = (u64)ctx->data_start_lba + (u64)(cluster - 2) * (u64)ctx->sectors_per_cluster;
    return driver_block_write_sectors_raw(ctx->dev_idx, (u64)lba,
                                 (u32)ctx->sectors_per_cluster, buf);
}

/* ---- Directory entry helpers (write support) ---- */

/* Read a 32-byte directory entry from disk. `dir_cluster` is the cluster
 * containing the entry; `entry_offset` is the byte offset within that cluster. */
static int fs_fat32_read_dirent(fs_fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset,
                             fs_fat32_dirent_t *out) {
    if (!ctx || !out || dir_cluster < 2) return -1;
    if (entry_offset + 32 > ctx->bytes_per_cluster) return -2;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -3;
    if (fs_fat32_read_cluster(ctx, dir_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -4;
    }
    memcpy(out, cbuf + entry_offset, sizeof(*out));
    kfree(cbuf);
    return 0;
}

/* Write a 32-byte directory entry to disk (read-modify-write the cluster). */
static int fs_fat32_write_dirent(fs_fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset,
                              const fs_fat32_dirent_t *in) {
    if (!ctx || !in || dir_cluster < 2) return -1;
    if (entry_offset + 32 > ctx->bytes_per_cluster) return -2;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -3;
    if (fs_fat32_read_cluster(ctx, dir_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -4;
    }
    memcpy(cbuf + entry_offset, in, sizeof(*in));
    int rc = fs_fat32_write_cluster(ctx, dir_cluster, cbuf);
    kfree(cbuf);
    return rc;
}

/* Update a directory entry's first_cluster and size on disk. */
static int fs_fat32_update_dir_entry(fs_fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset,
                                  u32 new_first_cluster, u64 new_size) {
    /* BUG-0191 FIX (A12-026): the on-disk file_size field is a u32
     * (fs.doc 5.1, maximum 4 GiB-1). The old code cast the u64 result
     * straight down, silently wrapping sizes >= 4 GiB while the
     * in-memory ino->size kept the full value - disk and memory then
     * disagreed about the file. Refuse sizes the field cannot represent
     * (callers fail the write instead of truncating silently). */
    if (new_size > 0xFFFFFFFFull) return -2;
    fs_fat32_dirent_t e;
    if (fs_fat32_read_dirent(ctx, dir_cluster, entry_offset, &e) < 0) return -1;
    e.first_cluster_hi = (u16)(new_first_cluster >> 16);
    e.first_cluster_lo = (u16)(new_first_cluster & 0xFFFFu);
    e.file_size = (u32)new_size;
    return fs_fat32_write_dirent(ctx, dir_cluster, entry_offset, &e);
}

/* BUG-0188 support: find the cluster whose FAT chain points at `cluster`.
 * O(chains) but deletion is rare. Returns 0 when none (start of chain). */
static u32 fs_fat32_prev_cluster_of(const fs_fat32_ctx_t *ctx, u32 cluster) {
    if (!ctx->fat_cache) return 0;
    u64 entries = ctx->fat_cache_size / 4;
    u64 limit = (u64)ctx->total_clusters + 2;
    if (limit > entries) limit = entries;
    for (u64 i = 2; i < limit; i++) {
        u32 v;
        memcpy(&v, ctx->fat_cache + i * 4, 4);
        if ((v & 0x0FFFFFFFu) == cluster) return (u32)i;
    }
    return 0;
}

/* BUG-0188: forward declaration - mark_entry_deleted (below) validates
 * the LFN run against the 8.3 checksum computed by this helper. */
static u8 fs_fat32_lfn_checksum(const u8 *short_name);
/* BUG-0187: forward declaration - find_entry (below) only trusts a
 * collected LFN run after this validator passed it. */
static int fs_fat32_lfn_run_valid(const u8 *lfn_buf, int lfn_count,
                                  const u8 sfn[11]);

/* Mark a directory entry as deleted (first byte = 0xE5). */
static int fs_fat32_mark_entry_deleted(fs_fat32_ctx_t *ctx, u32 dir_cluster, u32 entry_offset) {
    fs_fat32_dirent_t e;
    if (fs_fat32_read_dirent(ctx, dir_cluster, entry_offset, &e) < 0) return -1;
    /* Compute the LFN checksum over the ORIGINAL 11 name bytes BEFORE
     * name[0] is overwritten with the 0xE5 tombstone below. */
    u8 cksum = fs_fat32_lfn_checksum(e.name);
    e.name[0] = 0xE5;
    if (fs_fat32_write_dirent(ctx, dir_cluster, entry_offset, &e) < 0) return -1;

    /* WP-09-FIX BUG-009: also mark the LFN slots that precede the short
     * entry as deleted. Previously only the 8.3 entry got 0xE5, leaving
     * orphan LFN slots that were later glued onto the NEXT entry created
     * in this directory (e.g. after `rm file1`, a later `mkdir d1`
     * showed up as "d1file1" in tree/ls). LFN slots sit immediately
     * before the 8.3 entry (attr == 0x0F). */
    u32 off = entry_offset;
    u32 cur_cluster = dir_cluster;
    int guard = 0;
    for (;;) {
        /* BUG-0188 FIX (A12-023): the LFN walk used to stop at the
         * cluster boundary, so LFN slots for a file whose 8.3 entry
         * started a new cluster were never cleared (foreign-created long
         * names resurrected as "d1file1"-style glued entries). Walk the
         * cluster CHAIN backwards: when the walk reaches slot 0 of the
         * current cluster, continue at the PREVIOUS chain cluster and
         * set `off` one-past-its-end so the `off -= 32` below lands on
         * its last slot. fs_fat32_prev_cluster_of returns 0 at the start
         * of the chain; the guard bounds pathological FAT cycles. */
        if (off < 32) {
            u32 prev = fs_fat32_prev_cluster_of(ctx, cur_cluster);
            if (prev < 2) break;            /* start of the chain */
            cur_cluster = prev;
            off = (u32)ctx->sectors_per_cluster * ctx->bytes_per_sector;
        }
        off -= 32;
        if (++guard > 2048) break;
        fs_fat32_dirent_t l;
        if (fs_fat32_read_dirent(ctx, cur_cluster, off, &l) < 0) break;
        if (l.attr != FAT_ATTR_LFN) break;  /* reached a real 8.3 / end */
        /* The LFN slot's checksum (offset 13) must match the 8.3 name
         * being deleted - a mismatch means the run belongs to a
         * different entry and must be left alone. */
        if (((const u8 *)&l)[13] != cksum) break;
        l.name[0] = 0xE5;
        if (fs_fat32_write_dirent(ctx, cur_cluster, off, &l) < 0) break;
    }
    return 0;
}

/* Scan a directory for an entry matching `name` (8.3 display form). On success
 * returns 0 and fills the out_ params. Returns -1 if not found, -2 on I/O
 * error. */
/* P2-19 FIX: extract LFN name from collected LFN slots. */
static int fs_fat32_extract_lfn_name(const u8 *lfn_buf, int lfn_count, char *out, int out_len) {
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
    memcpy(out, name, copy);
    out[copy] = 0;
    return 0;
}

static int fs_fat32_find_entry(fs_fat32_ctx_t *ctx, u32 dir_first_cluster, const char *name,
                            u32 *out_cluster, u32 *out_offset,
                            fs_fat32_dirent_t *out_entry) {
    if (!ctx || dir_first_cluster < 2 || !name) return -2;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -2;
    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    u32 cluster = dir_first_cluster;
    int rc = -1;
    /* P2-19: LFN collection buffer. */
    u8 lfn_buf[20 * 32];
    int lfn_count = 0;
    u32 oc_hops = 0;   /* BUG-0058: bound the walk (corrupt chain = cycle) */
    while (cluster >= 2 && cluster < FAT_EOC && oc_hops++ < ctx->total_clusters + 2u) {
        if (fs_fat32_read_cluster(ctx, cluster, cbuf) < 0) { rc = -2; break; }
        for (int i = 0; i < entries_per_cluster; i++) {
            fs_fat32_dirent_t *e = (fs_fat32_dirent_t *)(cbuf + i * 32);
            if (e->name[0] == 0x00) { rc = -1; goto done; }
            if (e->name[0] == 0xE5) { lfn_count = 0; continue; }
            if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                /* P2-19: collect LFN entries. */
                if (lfn_count < 20) {
                    memcpy(lfn_buf + lfn_count * 32, e, 32);
                    lfn_count++;
                }
                continue;
            }
            if (e->attr & FAT_ATTR_VOLUME_ID) { lfn_count = 0; continue; }
            /* Try matching 8.3 name first. */
            char display[VFS_NAME_LEN];
            fs_fat32_format_short_name(e->name, display);
            int matched = (strcasecmp(display, name) == 0);
            /* P2-19: if no 8.3 match, try LFN name (validated per
             * BUG-0187: checksum + sequence). */
            if (!matched && lfn_count > 0 &&
                fs_fat32_lfn_run_valid(lfn_buf, lfn_count, e->name)) {
                char lfn_name[VFS_NAME_LEN];
                if (fs_fat32_extract_lfn_name(lfn_buf, lfn_count, lfn_name, sizeof(lfn_name)) == 0) {
                    if (strcasecmp(lfn_name, name) == 0) matched = 1;
                }
            }
            if (matched) {
                if (out_cluster) *out_cluster = cluster;
                if (out_offset) *out_offset = (u32)(i * 32);
                if (out_entry) memcpy(out_entry, e, sizeof(*out_entry));
                rc = 0;
                goto done;
            }
            lfn_count = 0;
        }
        cluster = fs_fat32_next_cluster(ctx, cluster);
    }
done:
    kfree(cbuf);
    return rc;
}

/* Find a free slot in a directory for a new 32-byte entry (0x00 or 0xE5).
 * Does NOT extend the dir cluster chain if full. Returns 0 on success. */
/* P2-19 FIX: find N consecutive free directory entry slots.
 * Returns 0 on success, fills out_cluster and out_offset with the first slot. */
static int fs_fat32_find_free_slots(fs_fat32_ctx_t *ctx, u32 dir_first_cluster,
                                 int num_slots, u32 *out_cluster, u32 *out_offset) {
    if (!ctx || dir_first_cluster < 2 || !out_cluster || !out_offset || num_slots < 1) return -1;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -2;
    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    u32 cluster = dir_first_cluster;
    int rc = -1;
    u32 oc_hops = 0;   /* BUG-0058: bound the walk (corrupt chain = cycle) */
    while (cluster >= 2 && cluster < FAT_EOC && oc_hops++ < ctx->total_clusters + 2u) {
        if (fs_fat32_read_cluster(ctx, cluster, cbuf) < 0) { rc = -2; break; }
        for (int i = 0; i <= entries_per_cluster - num_slots; i++) {
            int all_free = 1;
            for (int j = 0; j < num_slots; j++) {
                fs_fat32_dirent_t *e = (fs_fat32_dirent_t *)(cbuf + (i + j) * 32);
                if (e->name[0] != 0x00 && e->name[0] != 0xE5) { all_free = 0; break; }
            }
            if (all_free) {
                *out_cluster = cluster;
                *out_offset = (u32)(i * 32);
                rc = 0;
                goto done;
            }
        }
        cluster = fs_fat32_next_cluster(ctx, cluster);
    }
done:
    kfree(cbuf);
    return rc;
}

/* Encode a display name (e.g. "FOO.TXT") into an 11-byte 8.3 FAT name
 * (space-padded, uppercased). Returns 0 on success, -1 if the name doesn't
 * fit in 8.3 format. */
static int fs_fat32_encode_short_name(const char *name, u8 *out11) {
    memset(out11, ' ', 11);
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
static int fs_fat32_needs_lfn(const char *name) {
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
static int fs_fat32_write_lfn_entries(fs_fat32_ctx_t *ctx, u32 dir_cluster,
                                    const char *name, u8 sfn_checksum,
                                    u32 start_offset, u8 *cbuf) {
    (void)dir_cluster;  /* P3: unused — LFN entries are written to the same cluster buffer */
    int name_len = strlen(name);
    int num_lfn = (name_len + 12) / 13;  /* 13 chars per LFN entry */
    if (num_lfn > 20) return 0;  /* too long */

    /* Write LFN entries in reverse order (last entry first, with 0x40 flag). */
    for (int i = num_lfn - 1; i >= 0; i--) {
        u32 off = start_offset + (u32)(num_lfn - 1 - i) * 32;
        if (off + 32 > ctx->bytes_per_cluster) return 0;  /* simplified: single cluster */
        u8 *entry = cbuf + off;
        memset(entry, 0, 32);
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
static u8 fs_fat32_lfn_checksum(const u8 *short_name) {
    u8 sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (u8)((sum >> 1) | (sum << 7));
        sum += short_name[i];
    }
    return sum;
}

/* BUG-0187 FIX (A12-022), read side: validate a collected LFN run before
 * it is glued onto an 8.3 entry (fs.doc 7.4 "Name the Create Process
 * Gives to a Short Name" and the Microsoft LFN directory-entry format):
 * every slot must carry attr 0x0F, type 0, first-cluster word 0 and the
 * checksum of the 8.3 name; the sequence numbers must run N..1 with the
 * 0x40 terminal flag on the FIRST slot (LFN slots are stored in reverse
 * order). A damaged or glued run is ignored and the 8.3 name is used,
 * instead of silently reassembling garbage. Returns 1 when consistent. */
static int fs_fat32_lfn_run_valid(const u8 *lfn_buf, int lfn_count,
                                  const u8 sfn[11]) {
    if (lfn_count <= 0 || lfn_count > 20) return 0;
    u8 cksum = fs_fat32_lfn_checksum(sfn);
    for (int i = 0; i < lfn_count; i++) {
        const u8 *slot = lfn_buf + i * 32;
        u8 want = (u8)(lfn_count - i);
        if (i == 0) want |= 0x40;                     /* terminal flag */
        if (slot[0] != want) return 0;                /* sequence N..1 */
        if (slot[11] != FAT_ATTR_LFN) return 0;       /* attr */
        if (slot[12] != 0) return 0;                  /* type */
        if (slot[13] != cksum) return 0;              /* 8.3 checksum */
        if (slot[26] != 0 || slot[27] != 0) return 0; /* first cluster 0 */
    }
    return 1;
}

/* BUG-0187 support: map one long-name character to its 8.3 alias form
 * (fs.doc 7.4): ASCII letters are uppercased; characters outside the
 * legal short-name set become '_' (Windows drops spaces instead - a
 * cosmetic divergence that keeps the alias deterministic and, more
 * importantly, collision-checked below). */
static u8 fs_fat32_alias_char(char c) {
    if (c >= 'a' && c <= 'z') return (u8)(c - 32);
    if (c >= 'A' && c <= 'Z') return (u8)c;
    if (c >= '0' && c <= '9') return (u8)c;
    switch (c) {
        case '$': case '%': case '\'': case '-': case '_':
        case '@': case '~': case '`': case '!': case '(': case ')':
        case '{': case '}': case '^': case '#': case '&':
            return (u8)c;
        default:
            return (u8)'_';
    }
}

/* BUG-0187 support: 0 when an 11-byte short name is NOT in use in the dir. */
static int fs_fat32_shortname_free(fs_fat32_ctx_t *ctx, u32 dir_first_cluster,
                         const u8 cand[11]) {
    u32 c = dir_first_cluster;
    int guard = 0;
    while (c >= 2 && guard++ < 1024) {
        u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
        if (!cbuf) return -1;
        if (fs_fat32_read_cluster(ctx, c, cbuf) < 0) { kfree(cbuf); return -1; }
        u32 ents = ctx->bytes_per_cluster / 32;
        for (u32 k = 0; k < ents; k++) {
            const u8 *e = cbuf + k * 32;
            if (e[0] == 0x00) { kfree(cbuf); return 0; }   /* end of dir */
            if (e[0] == 0xE5) continue;                     /* deleted */
            if (e[11] == FAT_ATTR_LFN) continue;            /* LFN slot */
            if (memcmp(e, cand, 11) == 0) { kfree(cbuf); return 1; }
        }
        u32 nxt = fs_fat32_next_cluster(ctx, c);
        kfree(cbuf);
        if (nxt < 2 || nxt >= FAT_EOC) break;
        c = nxt;
    }
    return 0;
}

/* Create a new directory entry in `dir_first_cluster`. The entry has the given
 * name, attribute, first_cluster, and size. P2-19: now supports LFN.
 * Returns 0 on success. */
static int fs_fat32_create_entry(fs_fat32_ctx_t *ctx, u32 dir_first_cluster,
                              const char *name, u8 attr, u32 first_cluster, u64 size,
                              u32 *out_ec, u32 *out_eo) {
    u8 rawname[11];

    /* WP-09-FIX5 FIX: decide LFN first, then build the 8.3 entry name.
     * The old order ran fs_fat32_encode_short_name() BEFORE the LFN check,
     * so any name that does not fit 8.3 (e.g. "opencube.conf" with its
     * 4-character extension, or any lowercase name) failed the encode
     * and create_entry returned -1 before the LFN alias path could run:
     * creating such files on FAT32 was impossible even though reading
     * them (mtools/other OS created) worked. Now non-8.3 names go
     * straight to the LFN alias below; only genuinely unencodable 8.3
     * names are rejected. */

    /* P2-19: determine if we need LFN entries. */
    int needs_lfn = fs_fat32_needs_lfn(name);
    int num_lfn = 0;
    if (needs_lfn) {
        /* BUG-0187 FIX (A12-022): proper alias generation per the
         * Microsoft FAT spec 7.4. The old code emitted the first 6
         * uppercased chars + "~1" with NO extension and NO collision
         * check: two long names sharing a 6-char prefix produced two
         * IDENTICAL on-disk aliases (spec violation - other OSes'
         * behaviour is undefined), and "opencube.conf" lost its
         * extension ("OPENCU~1" instead of "OPENCU~1.CON").
         * Now: base (up to 6 chars) + "~N" (N=1..9, first free) +
         * the real extension (uppercased, 1-3 chars). Uniqueness is
         * verified against the directory's existing short entries. */
        char base[7];
        int nb = 0;
        const char *dot = 0;
        for (const char *q = name; *q; q++) if (*q == '.') dot = q;
        for (int q = 0; name[q] && name[q] != '.' && nb < 6; q++) {
            base[nb++] = (char)fs_fat32_alias_char(name[q]);
        }
        base[nb] = 0;
        char ext[4]; int ne = 0;
        if (dot && dot[1]) {
            for (const char *q = dot + 1; *q && ne < 3; q++) {
                ext[ne++] = (char)fs_fat32_alias_char(*q);
            }
        }
        ext[ne] = 0;
        int unique = 0;
        for (int tail = 1; tail <= 9 && !unique; tail++) {
            u8 cand[11];
            memset(cand, ' ', 11);
            int pos = 0;
            for (int q = 0; q < nb; q++) cand[pos++] = (u8)base[q];
            if (pos > 6) pos = 6;
            cand[pos++] = '~';
            cand[pos++] = (u8)('0' + tail);
            for (int q = 0; q < ne; q++) cand[8 + q] = (u8)ext[q];
            if (fs_fat32_shortname_free(ctx, dir_first_cluster, cand) == 0) {
                memcpy(rawname, cand, 11);
                unique = 1;
            }
        }
        if (!unique) return -1;   /* 9 tails all taken: caller creates a new name */
        if (name[0] == '.') { rawname[0] = '_'; }
        /* Calculate number of LFN entries needed. */
        int name_len = strlen(name);
        num_lfn = (name_len + 12) / 13;
        if (num_lfn > 20) return -1;
    } else {
        if (fs_fat32_encode_short_name(name, rawname) < 0) return -1;
    }

    /* P2-19: find num_lfn + 1 consecutive free slots. */
    u32 slot_cluster = 0, slot_offset = 0;
    if (fs_fat32_find_free_slots(ctx, dir_first_cluster, num_lfn + 1, &slot_cluster, &slot_offset) < 0) {
        return -2;
    }

    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -3;
    if (fs_fat32_read_cluster(ctx, slot_cluster, cbuf) < 0) {
        kfree(cbuf);
        return -4;
    }

    /* P2-19: write LFN entries before the 8.3 entry. */
    if (num_lfn > 0) {
        u8 cksum = fs_fat32_lfn_checksum(rawname);
        int written = fs_fat32_write_lfn_entries(ctx, slot_cluster, name, cksum,
                                               slot_offset, cbuf);
        if (written != num_lfn) {
            kfree(cbuf);
            return -6;
        }
    }

    /* Write the 8.3 entry after the LFN entries. */
    u32 sfn_offset = slot_offset + (u32)num_lfn * 32;
    fs_fat32_dirent_t *e = (fs_fat32_dirent_t *)(cbuf + sfn_offset);
    int was_end = (e->name[0] == 0x00);
    memset(e, 0, sizeof(*e));
    memcpy(e->name, rawname, 11);
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
        fs_fat32_dirent_t *next = (fs_fat32_dirent_t *)(cbuf + sfn_offset + 32);
        memset(next, 0, sizeof(*next));
    }
    if (fs_fat32_write_cluster(ctx, slot_cluster, cbuf) < 0) {
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
static int fs_fat32_dir_is_empty(fs_fat32_ctx_t *ctx, u32 dir_first_cluster) {
    if (!ctx || dir_first_cluster < 2) return -1;
    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) return -1;
    int entries_per_cluster = (int)(ctx->bytes_per_cluster / 32);
    u32 cluster = dir_first_cluster;
    int result = 1;
    u32 oc_hops = 0;   /* BUG-0058: bound the walk (corrupt chain = cycle) */
    while (cluster >= 2 && cluster < FAT_EOC && oc_hops++ < ctx->total_clusters + 2u) {
        if (fs_fat32_read_cluster(ctx, cluster, cbuf) < 0) { result = -1; break; }
        for (int i = 0; i < entries_per_cluster; i++) {
            fs_fat32_dirent_t *e = (fs_fat32_dirent_t *)(cbuf + i * 32);
            if (e->name[0] == 0x00) goto done;
            if (e->name[0] == 0xE5) continue;
            if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
            if (e->attr & FAT_ATTR_VOLUME_ID) continue;
            /* BUG-0056 FIX: every standard FAT directory starts with the
             * '.' and '..' entries. The emptiness scan never skipped
             * them, so fs_fat32_dir_is_empty() answered "not empty" for
             * EVERY freshly-created empty directory and rmdir() failed
             * on all of them. Compare the 8.3 body of the dot entries
             * ('.' followed by spaces, attr=directory) and skip them. */
            if ((e->attr & FAT_ATTR_DIRECTORY) &&
                    e->name[0] == '.' &&
                    (e->name[1] == '.' || e->name[1] == ' ') &&
                    e->name[2] == ' ' && e->name[3] == ' ' &&
                    e->name[4] == ' ' && e->name[5] == ' ' &&
                    e->name[6] == ' ' && e->name[7] == ' ' &&
                    e->name[8] == ' ' && e->name[9] == ' ' &&
                    e->name[10] == ' ')
                continue;   /* '.' or '..' */
            result = 0;
            goto done;
        }
        cluster = fs_fat32_next_cluster(ctx, cluster);
    }
done:
    kfree(cbuf);
    return result;
}

/* ---- VFS file operations ---- */

static int fs_fat32_open(fs_vfs_node_t *node, int flags) {
    if (!node) return -1;
    fs_fat32_inode_t *ino = (fs_fat32_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    fs_fat32_ctx_t *ctx = ino->ctx;
    /* P2-15 FIX: O_TRUNC — free the cluster chain, reset the inode, and
     * update the on-disk directory entry so subsequent reads of this file
     * return 0 bytes. Without this, `echo new > existing` would overwrite
     * only the first few bytes and leave the old tail bytes still
     * visible (the cluster chain stayed allocated, file_size unchanged). */
    if ((flags & VFS_O_TRUNC) && !ino->is_dir) {
        if (ino->start_cluster >= 2) {
            fs_fat32_free_cluster_chain(ctx, ino->start_cluster);
            ino->start_cluster = 0;
        }
        ino->size = 0;
        node->size = 0;
        if (node->parent) {
            fs_fat32_inode_t *pino = (fs_fat32_inode_t *)node->parent->private;
            if (pino && pino->start_cluster >= 2) {
                u32 ec = 0, eo = 0;
                if (fs_fat32_find_entry(ctx, pino->start_cluster, node->name,
                                     &ec, &eo, NULL) == 0) {
                    fs_fat32_update_dir_entry(ctx, ec, eo, 0, 0);
                }
            }
        }
    }
    /* If the VFS patched this node's type to FILE but the on-disk entry is
     * still a DIRECTORY (happens when fs_vfs_open's O_CREAT path calls
     * fs_fat32_mkdir then patches the in-memory type to FILE), convert the
     * on-disk entry to a regular file: change the attribute to ARCHIVE and
     * reset the size to 0. The first_cluster is kept (the empty cluster
     * allocated by fs_fat32_mkdir) so the first write can reuse it. */
    if (node->type == VFS_TYPE_FILE && ino->is_dir && node->parent) {
        fs_fat32_inode_t *pino = (fs_fat32_inode_t *)node->parent->private;
        if (pino && pino->start_cluster >= 2) {
            u32 ec = 0, eo = 0;
            fs_fat32_dirent_t e;
            if (fs_fat32_find_entry(ctx, pino->start_cluster, node->name,
                                 &ec, &eo, &e) == 0) {
                e.attr = FAT_ATTR_ARCHIVE;
                e.file_size = 0;
                fs_fat32_write_dirent(ctx, ec, eo, &e);
            }
        }
        ino->is_dir = 0;
        ino->size = 0;
        node->size = 0;
    }
    return 0;
}

static int fs_fat32_read(fs_vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    fs_fat32_inode_t *ino = (fs_fat32_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    if (ino->is_dir) return -3;
    if (ino->start_cluster < 2) return 0;     /* empty file */
    if (offset >= ino->size) return 0;
    u64 avail = ino->size - offset;
    int want = (int)(avail < (u64)size ? avail : (u64)size);

    fs_fat32_ctx_t *ctx = ino->ctx;
    u8 *cluster_buf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cluster_buf) return -4;

    /* Walk the chain to find the cluster containing `offset`. */
    u32 cluster = ino->start_cluster;
    u64 bpc = ctx->bytes_per_cluster;
    u64 skip_clusters = offset / bpc;
    u64 off_in_cluster = offset % bpc;
    for (u64 i = 0; i < skip_clusters; i++) {
        cluster = fs_fat32_next_cluster(ctx, cluster);
        if (cluster < 2 || cluster >= FAT_EOC) {
            kfree(cluster_buf);
            return 0;  /* chain shorter than expected */
        }
    }

    int copied = 0;
    while (copied < want) {
        if (cluster < 2 || cluster >= FAT_EOC) break;
        if (fs_fat32_read_cluster(ctx, cluster, cluster_buf) < 0) break;
        u64 in_this = bpc - off_in_cluster;
        u64 need = (u64)(want - copied);
        u64 to_copy = in_this < need ? in_this : need;
        memcpy((u8 *)buf + copied, cluster_buf + off_in_cluster, (usize)to_copy);
        copied += (int)to_copy;
        cluster = fs_fat32_next_cluster(ctx, cluster);
        off_in_cluster = 0;
    }
    kfree(cluster_buf);
    return copied;
}

static int fs_fat32_write(fs_vfs_node_t *node, u64 offset, const void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    fs_fat32_inode_t *ino = (fs_fat32_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    if (ino->is_dir) return -3;
    fs_fat32_ctx_t *ctx = ino->ctx;
    if (size == 0) return 0;
    /* BUG-0191 FIX (A12-026): fail up front when the resulting size
     * cannot be stored in the directory entry's u32 file_size field
     * (FAT32 maximum 4 GiB-1). Writing first and truncating later would
     * leave the on-disk entry wrapping around while ino->size (u64)
     * kept the real value. Sizes <= 4 GiB-1 keep working unchanged. */
    if (offset + (u64)size > 0xFFFFFFFFull) {
        return -1;
    }
    u64 bpc = ctx->bytes_per_cluster;
    u8 *cluster_buf = (u8 *)kmalloc(bpc);
    if (!cluster_buf) return -4;

    /* Allocate the first cluster if the file is empty; zero it on disk so
     * unwritten tail bytes read as 0. */
    if (ino->start_cluster < 2) {
        u32 c = fs_fat32_alloc_cluster(ctx);
        if (c < 2) { kfree(cluster_buf); return -5; }
        ino->start_cluster = c;
        memset(cluster_buf, 0, (usize)bpc);
        if (fs_fat32_write_cluster(ctx, c, cluster_buf) < 0) {
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
        u32 next = fs_fat32_next_cluster(ctx, cluster);
        if (next < 2 || next >= FAT_EOC) {
            u32 newc = fs_fat32_alloc_cluster(ctx);
            if (newc < 2) { kfree(cluster_buf); return -7; }
            /* BUG-0190 FIX (A12-025): append_cluster links `newc` into
             * the chain with a FAT-sector write; its status used to be
             * ignored, so a failed link left `newc` allocated but
             * UNLINKED while the write continued - data landed outside
             * the chain and the on-disk size claimed it was there.
             * Free the orphan and fail the write. */
            if (fs_fat32_append_cluster(ctx, cluster, newc) < 0) {
                fs_fat32_free_cluster_chain(ctx, newc);
                kfree(cluster_buf);
                return -7;
            }
            memset(cluster_buf, 0, (usize)bpc);
            if (fs_fat32_write_cluster(ctx, newc, cluster_buf) < 0) {
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
        if (fs_fat32_read_cluster(ctx, cluster, cluster_buf) < 0) {
            memset(cluster_buf, 0, (usize)bpc);
        }
        u64 in_this = bpc - off;
        u64 need = (u64)(size - written);
        u64 to_copy = in_this < need ? in_this : need;
        memcpy(cluster_buf + off, (const u8 *)buf + written, (usize)to_copy);
        if (fs_fat32_write_cluster(ctx, cluster, cluster_buf) < 0) break;
        written += (int)to_copy;
        off = 0;
        if (written < size) {
            u32 next = fs_fat32_next_cluster(ctx, cluster);
            if (next < 2 || next >= FAT_EOC) {
                u32 newc = fs_fat32_alloc_cluster(ctx);
                if (newc < 2) break;
                /* BUG-0190 FIX (A12-025): propagate the link failure -
                 * free the orphan cluster and stop the loop. `written`
                 * already holds only the bytes that really made it into
                 * the chain, so the size update below stays truthful. */
                if (fs_fat32_append_cluster(ctx, cluster, newc) < 0) {
                    fs_fat32_free_cluster_chain(ctx, newc);
                    break;
                }
                memset(cluster_buf, 0, (usize)bpc);
                if (fs_fat32_write_cluster(ctx, newc, cluster_buf) < 0) break;
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
        fs_fat32_inode_t *pino = (fs_fat32_inode_t *)node->parent->private;
        if (pino && pino->start_cluster >= 2) {
            u32 ec = 0, eo = 0;
            if (fs_fat32_find_entry(ctx, pino->start_cluster, node->name,
                                 &ec, &eo, NULL) == 0) {
                fs_fat32_update_dir_entry(ctx, ec, eo, ino->start_cluster, ino->size);
            }
        }
    }
    return written;
}

static u64 fs_fat32_seek(fs_vfs_node_t *node, u64 offset, int whence) {
    if (!node) return 0;
    u64 sz = node->size;
    u64 new_off = offset;
    switch (whence) {
        case VFS_SEEK_SET: new_off = offset; break;
        case VFS_SEEK_CUR: break;
        /* BUG-029 FIX (P3): the VFS layer pre-computes SEEK_END as
         * size + offset (absolute); the old size - offset here re-applied
         * the computation and made seek(fd,0,SEEK_END) return 0. */
        case VFS_SEEK_END: new_off = offset; break; /* pre-computed */
        default: break;
    }
    if (new_off > sz) new_off = sz;
    return new_off;
}

static int fs_fat32_close(fs_vfs_node_t *node) {
    (void)node;
    return 0;
}

static int fs_fat32_stat(fs_vfs_node_t *node, fs_vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- VFS directory operations ---- */

static int fs_fat32_mkdir(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fs_fat32_inode_t *pino = (fs_fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fs_fat32_ctx_t *ctx = pino->ctx;

    /* Refuse if the name already exists. */
    if (fs_fat32_find_entry(ctx, pino->start_cluster, name, NULL, NULL, NULL) == 0) {
        return -3;
    }

    /* Allocate a cluster for the new directory and zero it (empty dir). */
    u32 newc = fs_fat32_alloc_cluster(ctx);
    if (newc < 2) return -4;
    u8 *zbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!zbuf) {
        /* BUG-0186: free via the chain helper so the FSINFO free-count /
         * next-free hints stay current (was a raw set_fat_entry(0)). */
        fs_fat32_free_cluster_chain(ctx, newc);
        return -5;
    }
    memset(zbuf, 0, (usize)ctx->bytes_per_cluster);
    if (fs_fat32_write_cluster(ctx, newc, zbuf) < 0) {
        kfree(zbuf);
        fs_fat32_free_cluster_chain(ctx, newc);
        return -6;
    }
    kfree(zbuf);

    /* Create the directory entry in the parent. */
    if (fs_fat32_create_entry(ctx, pino->start_cluster, name, FAT_ATTR_DIRECTORY,
                           newc, 0, NULL, NULL) < 0) {
        fs_fat32_free_cluster_chain(ctx, newc);
        return -7;
    }
    return 0;
}

static int fs_fat32_rmdir(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fs_fat32_inode_t *pino = (fs_fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fs_fat32_ctx_t *ctx = pino->ctx;

    /* Find the entry. */
    u32 ec = 0, eo = 0;
    fs_fat32_dirent_t e;
    if (fs_fat32_find_entry(ctx, pino->start_cluster, name, &ec, &eo, &e) < 0) {
        return -3;  /* not found */
    }
    /* Must be a directory. */
    if (!(e.attr & FAT_ATTR_DIRECTORY)) {
        return -4;
    }
    /* Verify the dir is empty. */
    u32 fc = ((u32)e.first_cluster_hi << 16) | e.first_cluster_lo;
    if (fc >= 2) {
        int empty = fs_fat32_dir_is_empty(ctx, fc);
        if (empty != 1) {
            return -5;  /* not empty (or I/O error) */
        }
        /* Free the dir's cluster chain. */
        fs_fat32_free_cluster_chain(ctx, fc);
    }
    /* Mark the entry as deleted. */
    if (fs_fat32_mark_entry_deleted(ctx, ec, eo) < 0) {
        return -6;
    }
    return 0;
}

static fs_vfs_node_t *fs_fat32_lookup(fs_vfs_node_t *parent, const char *name);

static int fs_fat32_unlink(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    /* BUG-0061 FIX: unlinking while the file is open frees the cluster
     * chain; the still-open fd would then keep writing through a chain
     * that can be handed out again to another file (data resurrection /
     * cross-file corruption). Refuse until every fd is closed. */
    {
        fs_vfs_node_t *victim = fs_fat32_lookup(parent, name);
        if (victim && victim->open_count > 0) return -4;
    }
    fs_fat32_inode_t *pino = (fs_fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fs_fat32_ctx_t *ctx = pino->ctx;

    /* Find the entry. */
    u32 ec = 0, eo = 0;
    fs_fat32_dirent_t e;
    if (fs_fat32_find_entry(ctx, pino->start_cluster, name, &ec, &eo, &e) < 0) {
        return -3;  /* not found */
    }
    /* Refuse to unlink a directory (use rmdir for that). */
    if (e.attr & FAT_ATTR_DIRECTORY) {
        return -4;
    }
    /* Free the file's cluster chain. */
    u32 fc = ((u32)e.first_cluster_hi << 16) | e.first_cluster_lo;
    if (fc >= 2) {
        fs_fat32_free_cluster_chain(ctx, fc);
    }
    /* Mark the entry as deleted. */
    if (fs_fat32_mark_entry_deleted(ctx, ec, eo) < 0) {
        return -5;
    }
    return 0;
}

/* Read the entire directory into a kmalloc'd buffer of directory entries.
 * Returns the number of valid entries (excluding deleted & LFN), or a
 * negative value on error. `*out_buf` is kmalloc'd and must be kfree'd.
 *
 * BUG-0189 FIX (A12-024): the buffer used to be a hard 4096-entry
 * (128 KiB) cap and the chain walk stopped silently at it - larger
 * directories made lookup miss files in the second half and readdir
 * terminate early with no error. The buffer is now sized from the
 * directory's ACTUAL cluster chain: pass 1 counts the chain, pass 2
 * copies it. The only remaining bound is the volume's cluster count
 * (total_clusters + 2), which is chain-derived and exists purely to
 * terminate a corrupt FAT loop; hitting it is reported as an error
 * (-3) instead of silently returning a truncated directory. */
static int fs_fat32_read_dir_entries(fs_fat32_inode_t *dir, fs_fat32_dirent_t **out_buf) {
    if (!dir || !dir->is_dir || !dir->ctx || !out_buf) return -1;
    fs_fat32_ctx_t *ctx = dir->ctx;
    *out_buf = NULL;

    u32 cluster = dir->start_cluster;
    if (cluster < 2) {
        /* Empty directory (no cluster yet): zero entries, but hand back
         * a valid free-able buffer (callers kfree it unconditionally). */
        fs_fat32_dirent_t *buf = (fs_fat32_dirent_t *)kmalloc(32);
        if (!buf) return -2;
        *out_buf = buf;
        return 0;
    }

    /* Pass 1: count the clusters in the directory's chain. Any walk
     * longer than total_clusters + 2 must be looping (corrupt FAT). */
    u64 n_clusters = 0;
    {
        u32 c = cluster;
        u32 hops = 0;
        while (c >= 2 && c < FAT_EOC) {
            if (++hops > ctx->total_clusters + 2u) return -3;
            n_clusters++;
            c = fs_fat32_next_cluster(ctx, c);
        }
        if (n_clusters == 0) return -3;
    }

    /* Pass 2: one 32-byte slot per directory entry in every cluster. */
    u32 entries_per_cluster = ctx->bytes_per_cluster / 32;
    u64 max_entries = n_clusters * (u64)entries_per_cluster;
    /* The count is returned through an int - a directory whose entry
     * count cannot be represented there cannot be read in one buffer
     * anyway (it would need a >2 GiB allocation, which kmalloc refuses). */
    if (max_entries > 0x7FFFFFFFull) return -2;
    fs_fat32_dirent_t *buf = (fs_fat32_dirent_t *)kmalloc(max_entries * 32);
    if (!buf) return -2;

    u8 *cbuf = (u8 *)kmalloc(ctx->bytes_per_cluster);
    if (!cbuf) { kfree(buf); return -3; }

    u64 n = 0;
    u32 c = cluster;
    u32 hops = 0;
    while (c >= 2 && c < FAT_EOC && hops++ < ctx->total_clusters + 2u) {
        if (fs_fat32_read_cluster(ctx, c, cbuf) < 0) {
            kfree(cbuf);
            kfree(buf);
            return -4;
        }
        for (u32 i = 0; i < entries_per_cluster; i++) {
            memcpy(&buf[n], cbuf + i * 32, 32);
            n++;
        }
        c = fs_fat32_next_cluster(ctx, c);
    }
    kfree(cbuf);
    *out_buf = buf;
    return (int)n;
}

/* Count free clusters in the FAT. */
static u64 fs_fat32_count_free(fs_fat32_ctx_t *ctx) {
    if (!ctx->fat_cache) return 0;
    u64 free_count = 0;
    u64 entries = ctx->fat_cache_size / 4;
    for (u64 i = 0; i < entries; i++) {
        u32 v;
        memcpy(&v, ctx->fat_cache + i * 4, 4);
        if ((v & 0x0FFFFFFFu) == 0) free_count++;
    }
    return free_count;
}

static int fs_fat32_readdir(fs_vfs_node_t *dir, int index, fs_vfs_dirent_t *entry) {
    if (!dir || !entry) return -1;
    fs_fat32_inode_t *ino = (fs_fat32_inode_t *)dir->private;
    if (!ino) return -2;

    fs_fat32_dirent_t *buf = NULL;
    int n = fs_fat32_read_dir_entries(ino, &buf);
    if (n < 0) return -3;

    /* Walk entries, skipping deleted (0xE5), end-of-dir (0x00), LFN slots,
     * and volume-label entries. Map the index-th surviving entry.
     * P2-12 FIX: collect LFN slots before each 8.3 entry; when the 8.3
     * entry is the index-th survivor, prefer the assembled LFN long name
     * over the 8.3 short name. */
    u8 lfn_buf[20 * 32];
    int lfn_count = 0;
    int shown = 0;
    int rc = -4;
    for (int i = 0; i < n; i++) {
        fs_fat32_dirent_t *e = &buf[i];
        if (e->name[0] == 0x00) break;            /* end of dir */
        if (e->name[0] == 0xE5) { lfn_count = 0; continue; }  /* deleted */
        if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
            /* Collect LFN slots — these precede the 8.3 entry in reverse
             * order (last entry has 0x40 bit set in the order field). */
            if (lfn_count < 20) {
                memcpy(lfn_buf + lfn_count * 32, e, 32);
                lfn_count++;
            }
            continue;
        }
        if (e->attr & FAT_ATTR_VOLUME_ID) { lfn_count = 0; continue; }
        if (shown == index) {
            memset(entry, 0, sizeof(*entry));
            /* P2-12: prefer the long name if we collected a VALID LFN run
             * (BUG-0187: checksum + sequence validated; a damaged or
             * glued run falls back to the 8.3 name). */
            int have_lfn = 0;
            if (lfn_count > 0 &&
                fs_fat32_lfn_run_valid(lfn_buf, lfn_count, e->name)) {
                char lfn_name[VFS_NAME_LEN];
                if (fs_fat32_extract_lfn_name(lfn_buf, lfn_count,
                                           lfn_name, sizeof(lfn_name)) == 0
                        && lfn_name[0]) {
                    strncpy(entry->name, lfn_name, VFS_NAME_LEN - 1);
                    entry->name[VFS_NAME_LEN - 1] = 0;
                    have_lfn = 1;
                }
            }
            if (!have_lfn) {
                fs_fat32_format_short_name(e->name, entry->name);
            }
            entry->type = (e->attr & FAT_ATTR_DIRECTORY) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            u32 cluster = ((u32)e->first_cluster_hi << 16) | e->first_cluster_lo;
            entry->inode = cluster;
            rc = 0;
            break;
        }
        shown++;
        lfn_count = 0;
    }
    kfree(buf);
    return rc;
}

/* WP-09-FIX BUG-010: create a regular FILE entry (attr=0, cluster 0).
 * fs_vfs_open(O_CREAT) previously fell back to mkdir-then-type-patch for
 * FAT32 (this fs had no create op), which left a real DIRECTORY entry
 * on disk whenever a later step failed — e.g. a failed `mv` still left
 * a "[D] renamed" entry behind that no command could open. */
static int fs_fat32_create(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fs_fat32_inode_t *pino = (fs_fat32_inode_t *)parent->private;
    if (!pino || !pino->ctx || pino->start_cluster < 2) return -2;
    fs_fat32_ctx_t *ctx = pino->ctx;
    if (fs_fat32_create_entry(ctx, pino->start_cluster, name, 0, 0, 0,
                           NULL, NULL) < 0) {
        return -3;
    }
    return 0;
}

static fs_vfs_node_t *fs_fat32_lookup(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    fs_fat32_inode_t *pino = (fs_fat32_inode_t *)parent->private;
    if (!pino) return NULL;

    fs_fat32_dirent_t *buf = NULL;
    int n = fs_fat32_read_dir_entries(pino, &buf);
    if (n < 0) return NULL;

    fs_vfs_node_t *result = NULL;
    /* P2-19: collect LFN entries for long name matching. */
    u8 lfn_buf[20 * 32];
    int lfn_count = 0;
    for (int i = 0; i < n; i++) {
        fs_fat32_dirent_t *e = &buf[i];
        if (e->name[0] == 0x00) break;
        if (e->name[0] == 0xE5) { lfn_count = 0; continue; }
        if ((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
            /* P2-19: collect LFN entries. */
            if (lfn_count < 20) {
                memcpy(lfn_buf + lfn_count * 32, e, 32);
                lfn_count++;
            }
            continue;
        }
        if (e->attr & FAT_ATTR_VOLUME_ID) { lfn_count = 0; continue; }

        char display[VFS_NAME_LEN];
        fs_fat32_format_short_name(e->name, display);

        /* P2-19: try 8.3 match first, then LFN match (validated per
         * BUG-0187: checksum + sequence). */
        int matched = (strcasecmp(display, name) == 0);
        if (!matched && lfn_count > 0 &&
            fs_fat32_lfn_run_valid(lfn_buf, lfn_count, e->name)) {
            char lfn_name[VFS_NAME_LEN];
            if (fs_fat32_extract_lfn_name(lfn_buf, lfn_count, lfn_name, sizeof(lfn_name)) == 0) {
                if (strcasecmp(lfn_name, name) == 0) {
                    matched = 1;
                    /* Use the LFN name as the display name. */
                    strncpy(display, lfn_name, VFS_NAME_LEN - 1);
                    display[VFS_NAME_LEN - 1] = 0;
                }
            }
        }
        if (matched) {
            fs_fat32_inode_t *child_ino = (fs_fat32_inode_t *)kmalloc(sizeof(fs_fat32_inode_t));
            if (child_ino) {
                memset(child_ino, 0, sizeof(*child_ino));
                child_ino->ctx = pino->ctx;
                child_ino->start_cluster = ((u32)e->first_cluster_hi << 16) | e->first_cluster_lo;
                child_ino->is_dir = (e->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
                child_ino->size = e->file_size;
                fs_vfs_node_t *cn = fs_vfs_alloc_node(display,
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
        /* WP-09-FIX BUG-010: reset the LFN accumulation after each 8.3
         * entry (fs_fat32_readdir does the same). Without this, the second
         * file's LFN slots concatenated with the first file's in lfn_buf
         * and the long-name match always failed — every SECOND create
         * in a directory failed with "cannot create destination"/
         * "open failed" even though the entry was written to disk. */
        lfn_count = 0;
    }
    kfree(buf);
    return result;
}

/* ---- Mount / unmount ---- */

static fs_vfs_node_t *fs_fat32_fs_mount(const char *device) {
    /* WP-10a: mount ANY registered block device (IDE, AHCI SATA,
     * virtio-blk, NVMe) through the blk layer. */
    int dev_idx = fs_fat32_parse_device(device);
    if (dev_idx < 0) {
        screen_console_puts("fat32: invalid device string\n");
        return NULL;
    }
    driver_block_device_t *bdev = driver_block_get_device(dev_idx);
    if (!bdev) {
        screen_console_puts("fat32: drive not present\n");
        return NULL;
    }

    /* Read the boot sector. */
    u8 boot[512];
    if (driver_block_read_sectors_raw(dev_idx, 0, 1, boot) != 0) {
        screen_console_puts("fat32: read boot sector failed\n");
        return NULL;
    }
    fs_fat32_bpb_t *bpb = (fs_fat32_bpb_t *)boot;

    /* Sanity check. */
    /* P0fix1 BUG-0004 (A12-004): tighten BPB validation. The block layer
     * is fixed at 512-byte sectors, so anything else would read/write only
     * a fraction of every buffer; reserved_sectors must be >= 1 or
     * fat_start_lba becomes 0 and a later FAT write would overwrite the
     * boot sector / partition table. */
    if (bpb->bytes_per_sector != 512 ||
        bpb->sectors_per_cluster == 0 ||
        bpb->reserved_sectors < 1 ||
        bpb->num_fats == 0 || bpb->num_fats > 4) {
        /* BUG-0185: num_fats is also bounded (the FAT32 spec's own
         * formatter uses 2) so the mirror-write loop below stays
         * bounded on crafted volumes. */
        screen_console_puts("fat32: invalid BPB\n");
        return NULL;
    }
    /* Distinguish FAT32 from FAT12/16: root_entries == 0 and fat_size16 == 0. */
    if (bpb->root_entries != 0 || bpb->fat_size16 != 0) {
        screen_console_puts("fat32: not a FAT32 volume (FAT12/16 detected)\n");
        return NULL;
    }
    if (bpb->fat_size32 == 0) {
        screen_console_puts("fat32: fat_size32 == 0\n");
        return NULL;
    }

    fs_fat32_ctx_t *ctx = (fs_fat32_ctx_t *)kmalloc(sizeof(fs_fat32_ctx_t));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->dev_idx = dev_idx;
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
    if (ctx->total_clusters == 0) {
        screen_console_puts("fat32: volume has no data clusters\n");
        kfree(ctx);
        return NULL;
    }

    /* BUG-0185 FIX (A12-020), FAT32 side: BPB_ExtFlags (fs.doc 2.1.4,
     * "FAT32 Extended BPB"): bit 7 = FAT mirroring DISABLED; bits 0-3 =
     * the active FAT index, valid only when mirroring is disabled. The
     * driver used to ignore the field entirely - it always cached FAT #0
     * (wrong table on mirroring-disabled volumes that point at another
     * active FAT) and never updated the mirrors. Cache the ACTIVE FAT
     * below; fs_fat32_set_fat_entry replays writes to the other copies
     * while mirroring is enabled. */
    ctx->active_fat = 0;
    ctx->fat_mirror = 1;
    if (bpb->ext_flags & 0x0080u) {
        ctx->fat_mirror = 0;
        ctx->active_fat = (u32)(bpb->ext_flags & 0x000Fu);
        if (ctx->active_fat >= ctx->num_fats) {
            screen_console_puts("fat32: ext_flags active FAT out of range\n");
            kfree(ctx);
            return NULL;
        }
    }
    /* Point the FAT cache at the ACTIVE copy (FAT #0 on normal volumes). */
    ctx->fat_start_lba = ctx->reserved_sectors +
                         ctx->active_fat * ctx->fat_size_sectors;

    /* Cache the FAT. */
    ctx->fat_cache_size = (u64)ctx->fat_size_sectors * ctx->bytes_per_sector;
    ctx->fat_cache = (u8 *)kmalloc(ctx->fat_cache_size);
    if (!ctx->fat_cache) {
        screen_console_puts("fat32: FAT cache alloc failed\n");
        kfree(ctx);
        return NULL;
    }
    int rc = driver_block_read_sectors_raw(dev_idx, (u64)ctx->fat_start_lba,
                              (u32)ctx->fat_size_sectors, ctx->fat_cache);
    if (rc != 0) {
        screen_console_puts("fat32: FAT read failed\n");
        kfree(ctx->fat_cache);
        kfree(ctx);
        return NULL;
    }

    /* BUG-0186 FIX (A12-021): FSINFO sector handling (fs.doc "FSInfo
     * Sector" / BPB_FSInfo in the FAT32 Extended BPB 2.1.4). Layout:
     * lead signature 0x41615252 @ 0, structure signature 0x61417272
     * @ 484, free-cluster count @ 488, next-free cluster @ 492, trail
     * signature 0xAA550000 @ 508. Both hint fields are advisory and
     * 0xFFFFFFFF means "unknown". The driver used to ignore the
     * structure completely. Now: maintain the next-free pointer and
     * free count in memory on every alloc/free, persist them to the
     * FSINFO sector, and (re)build the signature frame when the sector
     * is blank or damaged - which is the normal state on our own
     * mkfs.fat32 volumes (mkfs stamps BPB_FSInfo but zeroes the
     * reserved area afterwards). Volumes formatted without an FSINFO
     * sector at all (BPB_FSInfo = 0 or 0xFFFF) are handled honestly:
     * the hints are still maintained in RAM, nothing is written. */
    if (bpb->fs_info_sector != 0xFFFF && bpb->fs_info_sector != 0 &&
        (u32)bpb->fs_info_sector < ctx->data_start_lba) {
        u8 *sec = (u8 *)kmalloc(ctx->bytes_per_sector);
        if (sec) {
            int valid = 0;
            if (driver_block_read_sectors_raw(dev_idx, bpb->fs_info_sector,
                                              1, sec) == 0) {
                u32 lead, strt, trail;
                memcpy(&lead, sec, 4);
                memcpy(&strt, sec + 484, 4);
                memcpy(&trail, sec + 508, 4);
                valid = (lead == 0x41615252u && strt == 0x61417272u &&
                         trail == 0xAA550000u);
            }
            if (!valid) {
                u32 lead = 0x41615252u, strt = 0x61417272u, trail = 0xAA550000u;
                memset(sec, 0, ctx->bytes_per_sector);
                memcpy(sec, &lead, 4);
                memcpy(sec + 484, &strt, 4);
                memcpy(sec + 508, &trail, 4);
            }
            ctx->fsinfo_lba = bpb->fs_info_sector;
            ctx->fsinfo_sector = sec;
            u32 disk_next_free;
            memcpy(&disk_next_free, sec + 492, 4);
            if (disk_next_free >= 2 && disk_next_free < ctx->total_clusters + 2)
                ctx->next_free_hint = disk_next_free;
        }
    }
    /* Recompute the free-cluster count from the freshly cached FAT
     * (entries in [2, total_clusters + 2)); this also repairs a stale
     * count for other OSes' free-space reporting. The next-free hint
     * from disk (when valid) seeds the allocation scan. */
    {
        u32 free_count = 0;
        u64 entries = ctx->fat_cache_size / 4;
        u64 limit = (u64)ctx->total_clusters + 2;
        if (limit > entries) limit = entries;
        for (u64 i = 2; i < limit; i++) {
            u32 v;
            memcpy(&v, ctx->fat_cache + i * 4, 4);
            if ((v & 0x0FFFFFFFu) == 0) free_count++;
        }
        ctx->free_count_hint = free_count;
        if (ctx->next_free_hint < 2 || ctx->next_free_hint >= ctx->total_clusters + 2)
            ctx->next_free_hint = 2;
    }

    g_last_ctx = ctx;

    /* Create the root VFS node. */
    fs_fat32_inode_t *root_ino = (fs_fat32_inode_t *)kmalloc(sizeof(fs_fat32_inode_t));
    if (!root_ino) {
        kfree(ctx->fat_cache);
        /* BUG-0186: match the unmount teardown - the FSINFO scratch
         * sector is alive at this point and would leak. */
        if (ctx->fsinfo_sector) kfree(ctx->fsinfo_sector);
        kfree(ctx);
        return NULL;
    }
    memset(root_ino, 0, sizeof(*root_ino));
    root_ino->ctx = ctx;
    root_ino->start_cluster = ctx->root_cluster;
    root_ino->is_dir = 1;
    root_ino->size = 0;

    fs_vfs_node_t *root = fs_vfs_alloc_node("", VFS_TYPE_DIR, &g_fat32_fs_type);
    if (!root) {
        kfree(root_ino);
        kfree(ctx->fat_cache);
        /* BUG-0186: match the unmount teardown - the FSINFO scratch
         * sector is alive at this point and would leak. */
        if (ctx->fsinfo_sector) kfree(ctx->fsinfo_sector);
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
static void fs_fat32_free_subtree(fs_vfs_node_t *node) {
    if (!node) return;
    /* Recursively free children first. */
    fs_vfs_node_t *child = node->first_child;
    while (child) {
        fs_vfs_node_t *next = child->next_sibling;
        fs_fat32_free_subtree(child);
        child = next;
    }
    /* Free this node's private data (inode). */
    fs_fat32_inode_t *ino = (fs_fat32_inode_t *)node->private;
    if (ino) kfree(ino);
    /* Free the node itself. */
    kfree(node);
}

static int fs_fat32_fs_unmount(fs_vfs_node_t *root) {
    if (!root) return -1;
    /* BUG-027 FIX: Free the entire VFS subtree, not just the root. */
    fs_fat32_inode_t *ino = (fs_fat32_inode_t *)root->private;
    if (ino) {
        fs_fat32_ctx_t *ctx = ino->ctx;
        if (ctx) {
            /* BUG-0057 FIX: g_last_ctx is published by fs_fat32_fs_mount()
             * (fs_fat32_get_stats / `df` reads it). It was never cleared
             * on unmount, so any `df` after `umount` dereferenced the
             * freed context (use-after-free). Clear it before freeing,
             * under the same guard the mount path uses. */
            if (g_last_ctx == ctx) g_last_ctx = NULL;
            if (ctx->fat_cache) kfree(ctx->fat_cache);
            /* BUG-0186: the FSINFO scratch sector is allocated at mount
             * alongside the FAT cache - release it here too or every
             * mount/unmount cycle leaks one 512-byte block. */
            if (ctx->fsinfo_sector) kfree(ctx->fsinfo_sector);
            kfree(ctx);
        }
    }
    /* Recursively free all nodes + inodes in the tree. */
    fs_fat32_free_subtree(root);
    return 0;
}

/* ---- Init ---- */

void fs_fat32_init(void) {
    memset(&g_fat32_fs_type, 0, sizeof(g_fat32_fs_type));
    strncpy(g_fat32_fs_type.name, "fat32", sizeof(g_fat32_fs_type.name) - 1);
    g_fat32_fs_ops.mount   = fs_fat32_fs_mount;
    g_fat32_fs_ops.unmount = fs_fat32_fs_unmount;
    g_fat32_file_ops.open  = fs_fat32_open;
    g_fat32_file_ops.read  = fs_fat32_read;
    g_fat32_file_ops.write = fs_fat32_write;
    g_fat32_file_ops.seek  = fs_fat32_seek;
    g_fat32_file_ops.close = fs_fat32_close;
    g_fat32_file_ops.stat  = fs_fat32_stat;
    g_fat32_dir_ops.mkdir   = fs_fat32_mkdir;
    g_fat32_dir_ops.rmdir   = fs_fat32_rmdir;
    g_fat32_dir_ops.readdir = fs_fat32_readdir;
    g_fat32_dir_ops.lookup  = fs_fat32_lookup;
    g_fat32_dir_ops.unlink  = fs_fat32_unlink;
    g_fat32_dir_ops.create  = fs_fat32_create;  /* WP-09-FIX BUG-010 */
    /* rename is not supported; the VFS layer falls back to copy+unlink. */
    g_fat32_fs_type.fs_ops   = &g_fat32_fs_ops;
    g_fat32_fs_type.file_ops = &g_fat32_file_ops;
    g_fat32_fs_type.dir_ops  = &g_fat32_dir_ops;

    fs_vfs_register_fs("fat32", &g_fat32_fs_ops, &g_fat32_file_ops, &g_fat32_dir_ops);
    /* BUG-0179 FIX (A12-014): FAT32 name matching is case-insensitive
     * (fs_fat32_lookup/find_entry use strcasecmp) - tell the VFS node
     * cache so cached and uncached lookups agree. */
    g_fat32_fs_type.case_insensitive = 1;
    fs_vfs_set_fs_case_insensitive("fat32", 1);
    screen_console_puts("fat32: registered (read/write)\n");
}

int fs_fat32_mount(const char *device, const char *mount_point) {
    return fs_vfs_mount("fat32", mount_point, device);
}

void fs_fat32_get_stats(u64 *total_sectors, u64 *free_clusters, u32 *cluster_size) {
    if (total_sectors) *total_sectors = 0;
    if (free_clusters) *free_clusters = 0;
    if (cluster_size)  *cluster_size  = 0;
    if (!g_last_ctx) return;
    if (total_sectors) *total_sectors = (u64)g_last_ctx->total_clusters * g_last_ctx->sectors_per_cluster;
    if (free_clusters) *free_clusters = fs_fat32_count_free(g_last_ctx);
    if (cluster_size)  *cluster_size  = g_last_ctx->bytes_per_cluster;
}
