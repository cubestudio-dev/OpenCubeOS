/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/ext4.c
 * Purpose: ext4 filesystem driver (READ-ONLY) for the VFS.
 *
 * Uses the ORIGINAL WP-05 VFS interface (3 separate ops structs:
 * fs_vfs_fs_ops_t, fs_vfs_file_ops_t, fs_vfs_dir_ops_t). The fs-wide state lives
 * in an fs_ext4_ctx_t stored off the root node's private (via the root
 * inode). Each looked-up node carries an fs_ext4_inode_t in its private.
 *
 * Supports: mount, open/close/read/seek, stat, readdir, lookup.
 * Mutating ops (write/mkdir/rmdir/unlink/rename) all return -1.
 *
 * Handles both the modern extent-tree block mapping (i_flags & 0x80000)
 * and the legacy direct/indirect/double-indirect/triple-indirect block
 * pointers (ext2/3 style). Directory entries use the linked-list format
 * (rec_len, name_len, file_type). 64-bit block group descriptors are
 * supported when s_desc_size > 32.
 *
 * On-disk structures use #pragma pack(push, 1). All functions are static
 * except fs_ext4_init. Memory comes from kmalloc/kfree; sector I/O goes
 * through driver_block_ata_read_sectors (drive 0..3, LBA 0-based).
 *
 * The `device` string passed to mount is parsed into a drive number 0..3
 * (e.g. "ata0" -> 0). ext4 is mounted on the whole disk starting at LBA 0
 * (no partition offset). The superblock is read from byte 1024 (sector 2).
 */
#include "fs_ext4.h"
#include "types.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"
#include "fs_vfs.h"
#include "driver_block_ata.h"

#pragma pack(push, 1)
typedef struct {
    u32 s_inodes_count;
    u32 s_blocks_count_lo;
    u32 s_r_blocks_count_lo;
    u32 s_free_blocks_count_lo;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_cluster_size;
    u32 s_blocks_per_group;
    u32 s_clusters_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8  s_uuid[16];
    u8  s_volume_name[16];
    u8  s_last_mounted[64];
    u32 s_algorithm_usage_bitmap;
    u8  s_prealloc_blocks;
    u8  s_prealloc_dir_blocks;
    u16 s_reserved_gdt_blocks;
    u8  s_journal_uuid[16];
    u32 s_journal_inum;
    u32 s_journal_dev;
    u32 s_last_orphan;
    u32 s_hash_seed[4];
    u8  s_def_hash_version;
    u8  s_jnl_backup_type;
    u16 s_desc_size;
    u32 s_default_mount_opts;
    u32 s_first_meta_bg;
    u32 s_blocks_count_hi;
    u32 s_feature_incompat_hi;
    u32 s_feature_ro_compat_hi;
    u8  s_reserved2[88];
} fs_ext4_sb_t;

typedef struct {
    u32 bg_block_bitmap_lo;
    u32 bg_inode_bitmap_lo;
    u32 bg_inode_table_lo;
    u16 bg_free_blocks_count_lo;
    u16 bg_free_inodes_count_lo;
    u16 bg_used_dirs_count_lo;
    u16 bg_flags;
    u32 bg_exclude_bitmap_lo;
    u16 bg_block_bitmap_csum_lo;
    u16 bg_inode_bitmap_csum_lo;
    u16 bg_itable_unused_lo;
    u16 bg_checksum;
    u32 bg_block_bitmap_hi;
    u32 bg_inode_bitmap_hi;
    u32 bg_inode_table_hi;
    u16 bg_free_blocks_count_hi;
    u16 bg_free_inodes_count_hi;
    u16 bg_used_dirs_count_hi;
    u16 bg_itable_unused_hi;
    u32 bg_exclude_bitmap_hi;
    u16 bg_block_bitmap_csum_hi;
    u16 bg_inode_bitmap_csum_hi;
} fs_ext4_group_desc_t;

typedef struct {
    u16 i_mode;
    u16 i_uid;
    u32 i_size_lo;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks_lo;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[15];
    u32 i_generation;
    u32 i_file_acl_lo;
    u32 i_size_hi;        /* also i_dir_acl in legacy */
    u32 i_faddr;
    u8  i_osd2[12];
} fs_ext4_raw_inode_t;

typedef struct {
    u16 eh_magic;
    u16 eh_entries;
    u16 eh_max;
    u16 eh_depth;
    u32 eh_generation;
} fs_ext4_extent_header_t;

typedef struct {
    u32 ee_block;
    u16 ee_len;
    u16 ee_start_hi;
    u32 ee_start_lo;
} fs_ext4_extent_t;

typedef struct {
    u32 ei_block;
    u16 ei_leaf_lo;
    u16 ei_leaf_hi;
    u32 ei_unused;
} fs_ext4_extent_idx_t;

typedef struct {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    u8  name[255];
} fs_ext4_dirent_t;
#pragma pack(pop)

#define EXT4_MAGIC          0xEF53
#define EXT4_EXTENTS_FLAG   0x80000u
#define EXT4_ROOT_INO       2
#define EXT4_S_IFMT         0xF000
#define EXT4_S_IFDIR        0x4000
#define EXT4_S_IFREG        0x8000
#define EXT4_S_IFLNK        0xA000
#define EXT4_EXT_MAGIC      0xF30A

/* FS-wide state. Lives in the root node's inode->ctx. */
typedef struct {
    int  drive;
    u32  block_size;
    u32  inodes_count;
    u32  blocks_count;
    u32  inodes_per_group;
    u32  blocks_per_group;
    u32  inode_size;
    u32  desc_size;
    u32  groups_count;
} fs_ext4_ctx_t;

/* Per-node state (file or directory). Lives in fs_vfs_node_t->private. */
typedef struct {
    fs_ext4_ctx_t *ctx;
    u32  inode_num;
    u64  file_size;
    u32  i_flags;
    u32  i_block[15];
} fs_ext4_inode_t;

static fs_vfs_fs_type_t  g_ext4_fs_type;
static fs_vfs_fs_ops_t   g_ext4_fs_ops;
static fs_vfs_file_ops_t g_ext4_file_ops;
static fs_vfs_dir_ops_t  g_ext4_dir_ops;

/* ---- Device string parsing ---- */

static int fs_ext4_parse_device(const char *device) {
    if (!device) return 0;
    if (strcmp(device, "ata") == 0) return 0;
    if (strncmp(device, "ata", 3) == 0) {
        char c = device[3];
        if (c >= '0' && c <= '3' && device[4] == 0) return c - '0';
        return -1;
    }
    /* P2-05 FIX: accept "hda".."hdd" (standard block device names) in
     * addition to "ata0".."ata3" and "0".."3". This mirrors the fix
     * already applied to fs_exfat_parse_device (P1-2). Without this,
     * `mount ext4 hda /x` fails. */
    if (strncmp(device, "hd", 2) == 0) {
        char c = device[2];
        if (c >= 'a' && c <= 'd' && device[3] == 0) return c - 'a';
        return -1;
    }
    if (device[0] >= '0' && device[0] <= '3' && device[1] == 0) {
        return device[0] - '0';
    }
    return -1;
}

/* ---- Sector / block I/O ---- */

static int fs_ext4_read_block(fs_ext4_ctx_t *ctx, u64 block, void *buf) {
    u32 sectors_per_block = ctx->block_size / 512;
    u64 lba = block * sectors_per_block;
    int rc = driver_block_ata_read_sectors(ctx->drive, lba, (int)sectors_per_block, buf);
    return rc == (int)sectors_per_block ? 0 : -1;
}

/* ---- Superblock + group descriptors ---- */

static int fs_ext4_load_sb(fs_ext4_ctx_t *ctx, fs_ext4_sb_t *sb) {
    /* The superblock lives at byte 1024 (sector 2). It is 1024 bytes
     * wide on disk; our struct is ~364 bytes so reading 1 sector (512
     * bytes) covers it. We read 2 sectors to be safe. */
    u8 buf[1024];
    int rc = driver_block_ata_read_sectors(ctx->drive, 2, 2, buf);
    if (rc != 2) return -1;
    memcpy(sb, buf, sizeof(*sb));
    return 0;
}

static int fs_ext4_load_group_desc(fs_ext4_ctx_t *ctx, u32 group,
                                fs_ext4_group_desc_t *gd) {
    u8 buf[512];
    u32 desc_size = ctx->desc_size ? ctx->desc_size : 32;
    if (desc_size > sizeof(*gd)) desc_size = sizeof(*gd);
    /* Descriptor table starts at the block after the superblock. */
    u64 desc_byte_offset;
    if (ctx->block_size == 1024) {
        /* SB is in block 1; desc table starts at block 2. */
        desc_byte_offset = (u64)2 * ctx->block_size + (u64)group * desc_size;
    } else {
        /* SB is at byte 1024 inside block 0; desc table starts at block 1. */
        desc_byte_offset = (u64)1 * ctx->block_size + (u64)group * desc_size;
    }
    u64 desc_sector = desc_byte_offset / 512;
    u32 off_in_sec = (u32)(desc_byte_offset % 512);
    /* BUG-0062 FIX: a crafted sector/cluster geometry can push the
     * descriptor across the sector boundary; the unbounded memcpy then
     * read past the 512-byte stack buffer. Also reject groups that are
     * obviously out of range before doing any I/O. */
    if (off_in_sec + desc_size > 512) return -1;
    int rc = driver_block_ata_read_sectors(ctx->drive, desc_sector, 1, buf);
    if (rc != 1) return -1;
    memcpy(gd, buf + off_in_sec, desc_size);
    return 0;
}

/* Read an inode by number into the caller-provided struct. */
static int fs_ext4_read_inode(fs_ext4_ctx_t *ctx, u32 inode_num,
                           fs_ext4_raw_inode_t *inode) {
    if (inode_num == 0) return -1;
    if (ctx->inodes_per_group == 0) return -1;
    /* BUG-0062 FIX: bound the inode number so a crafted superblock
     * (huge inodes_count with tiny inodes_per_group) cannot push the
     * descriptor read arbitrarily far out of the table. */
    u32 max_groups = (ctx->inodes_count + ctx->inodes_per_group - 1) /
                     ctx->inodes_per_group;
    if (max_groups == 0) return -1;
    u32 group = (inode_num - 1) / ctx->inodes_per_group;
    u32 index = (inode_num - 1) % ctx->inodes_per_group;
    if (group >= max_groups) return -1;
    fs_ext4_group_desc_t gd;
    if (fs_ext4_load_group_desc(ctx, group, &gd) != 0) return -1;
    u64 inode_table_block = gd.bg_inode_table_lo;
    if (ctx->desc_size >= 64) {
        inode_table_block |= (u64)gd.bg_inode_table_hi << 32;
    }
    u64 byte_offset = inode_table_block * ctx->block_size +
                      (u64)index * ctx->inode_size;
    u64 block = byte_offset / ctx->block_size;
    u32 off_in_block = (u32)(byte_offset % ctx->block_size);
    /* BUG-0062 FIX: the raw inode must fit inside the block we read;
     * a crafted inode_size could otherwise push the memcpy past the
     * end of the block buffer. */
    if (ctx->inode_size == 0 ||
        off_in_block + (u32)sizeof(*inode) > ctx->block_size ||
        (u32)sizeof(*inode) > ctx->inode_size) {
        return -1;
    }
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    if (fs_ext4_read_block(ctx, block, buf) != 0) { kfree(buf); return -1; }
    memcpy(inode, buf + off_in_block, sizeof(*inode));
    kfree(buf);
    return 0;
}

/* ---- Block mapping ---- */

/* Map a logical block index to a physical block number via the extent
 * tree starting at the inode's i_block[0..] (root header). Returns
 * physical block, or 0 if unmapped. */
static u64 fs_ext4_extent_lookup(fs_ext4_ctx_t *ctx, const u32 *i_block,
                              u32 logical) {
    const fs_ext4_extent_header_t *hdr = (const fs_ext4_extent_header_t *)i_block;
    if (hdr->eh_magic != EXT4_EXT_MAGIC) return 0;
    /* P1-8 FIX: bound eh_entries to prevent OOB reads on corrupt disks.
     * The inode i_block array is 60 bytes = 15 u32s. The header is 12 bytes
     * (3 u32s), leaving 48 bytes = 12 u32s for entries. Each extent entry
     * is 12 bytes (3 u32s), so max 4 entries in the root. Each index entry
     * is also 12 bytes, so max 4 indices. */
    u16 max_entries = 4;  /* (60 - 12) / 12 */
    u16 n_entries = hdr->eh_entries;
    if (n_entries > max_entries) n_entries = max_entries;
    if (hdr->eh_depth == 0) {
        /* BUG-0060 FIX: extent entries start at i_block + 12 bytes (the
         * extent header is 12 bytes: magic/entries/max/depth/generation).
         * The old pointer arithmetic used i_block + 4 where i_block is a
         * u32 pointer, i.e. +16 bytes - every extent lookup on the root
         * node read the wrong entry. */
        const fs_ext4_extent_t *ext =
            (const fs_ext4_extent_t *)((const u8 *)i_block + 12);
        for (u16 i = 0; i < n_entries; i++) {
            if (logical >= ext[i].ee_block &&
                logical < ext[i].ee_block + ext[i].ee_len) {
                u64 phys = (u64)ext[i].ee_start_lo |
                           ((u64)ext[i].ee_start_hi << 32);
                return phys + (logical - ext[i].ee_block);
            }
        }
        return 0;
    }
    /* Index level: walk down. We support one level of indirection. */
    const fs_ext4_extent_idx_t *idx = (const fs_ext4_extent_idx_t *)(i_block + 4);
    for (u16 i = 0; i < n_entries; i++) {
        if (logical >= idx[i].ei_block) {
            if (i + 1 < hdr->eh_entries && logical >= idx[i + 1].ei_block)
                continue;
            u64 child = (u64)idx[i].ei_leaf_lo |
                        ((u64)idx[i].ei_leaf_hi << 32);
            u8 *child_buf = (u8 *)kmalloc(ctx->block_size);
            if (!child_buf) return 0;
            if (fs_ext4_read_block(ctx, child, child_buf) != 0) {
                kfree(child_buf);
                return 0;
            }
            const fs_ext4_extent_header_t *ch =
                (const fs_ext4_extent_header_t *)child_buf;
            if (ch->eh_magic != EXT4_EXT_MAGIC) {
                kfree(child_buf);
                return 0;
            }
            /* BUG-0060 FIX: same header-size rule at the child level -
             * entries start at child_buf + 12, not +8. */
            const fs_ext4_extent_t *ext =
                (const fs_ext4_extent_t *)(child_buf + 12);
            u64 result = 0;
            /* P1-8 FIX: bound child entries to block_size / 12. */
            u16 max_child = (u16)(ctx->block_size / 12);
            u16 n_child = ch->eh_entries;
            if (n_child > max_child) n_child = max_child;
            for (u16 j = 0; j < n_child; j++) {
                if (logical >= ext[j].ee_block &&
                    logical < ext[j].ee_block + ext[j].ee_len) {
                    u64 phys = (u64)ext[j].ee_start_lo |
                               ((u64)ext[j].ee_start_hi << 32);
                    result = phys + (logical - ext[j].ee_block);
                    break;
                }
            }
            kfree(child_buf);
            return result;
        }
    }
    return 0;
}

/* Legacy (ext2/3) block mapping: direct, indirect, double, triple. */
static u64 fs_ext4_legacy_lookup(fs_ext4_ctx_t *ctx, const u32 *i_block,
                              u32 logical) {
    u32 ptrs_per_block = ctx->block_size / 4;
    if (logical < 12) {
        return i_block[logical];
    }
    u32 off = logical - 12;
    if (off < ptrs_per_block) {
        u32 ind_block = i_block[12];
        if (ind_block == 0) return 0;
        u32 *buf = (u32 *)kmalloc(ctx->block_size);
        if (!buf) return 0;
        if (fs_ext4_read_block(ctx, ind_block, buf) != 0) { kfree(buf); return 0; }
        u32 r = buf[off];
        kfree(buf);
        return r;
    }
    off -= ptrs_per_block;
    if (off < ptrs_per_block * ptrs_per_block) {
        u32 dind_block = i_block[13];
        if (dind_block == 0) return 0;
        u32 *buf = (u32 *)kmalloc(ctx->block_size);
        if (!buf) return 0;
        if (fs_ext4_read_block(ctx, dind_block, buf) != 0) { kfree(buf); return 0; }
        u32 ind = buf[off / ptrs_per_block];
        kfree(buf);
        if (ind == 0) return 0;
        buf = (u32 *)kmalloc(ctx->block_size);
        if (!buf) return 0;
        if (fs_ext4_read_block(ctx, ind, buf) != 0) { kfree(buf); return 0; }
        u32 r = buf[off % ptrs_per_block];
        kfree(buf);
        return r;
    }
    off -= ptrs_per_block * ptrs_per_block;
    /* Triple indirect. */
    u32 tind_block = i_block[14];
    if (tind_block == 0) return 0;
    u32 *buf = (u32 *)kmalloc(ctx->block_size);
    if (!buf) return 0;
    if (fs_ext4_read_block(ctx, tind_block, buf) != 0) { kfree(buf); return 0; }
    u32 dind = buf[off / (ptrs_per_block * ptrs_per_block)];
    kfree(buf);
    if (dind == 0) return 0;
    off %= ptrs_per_block * ptrs_per_block;
    buf = (u32 *)kmalloc(ctx->block_size);
    if (!buf) return 0;
    if (fs_ext4_read_block(ctx, dind, buf) != 0) { kfree(buf); return 0; }
    u32 ind = buf[off / ptrs_per_block];
    kfree(buf);
    if (ind == 0) return 0;
    buf = (u32 *)kmalloc(ctx->block_size);
    if (!buf) return 0;
    if (fs_ext4_read_block(ctx, ind, buf) != 0) { kfree(buf); return 0; }
    u32 r = buf[off % ptrs_per_block];
    kfree(buf);
    return r;
}

static u64 fs_ext4_logical_to_physical(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *ino,
                                    u32 logical) {
    if (ino->i_flags & EXT4_EXTENTS_FLAG) {
        return fs_ext4_extent_lookup(ctx, ino->i_block, logical);
    }
    return fs_ext4_legacy_lookup(ctx, ino->i_block, logical);
}

/* ---- Directory iteration ---- */

/* Find a directory entry named `name` inside the directory described by
 * `dir_ino`. On success returns 0 and fills *out_inode with the child
 * inode number. Returns -1 on error or not found. */
static int fs_ext4_find_in_dir(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *dir_ino,
                            const char *name, u32 *out_inode) {
    int name_len = (int)strlen(name);
    u32 n_blocks = (u32)(dir_ino->file_size / ctx->block_size);
    if (n_blocks == 0) n_blocks = 1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    for (u32 b = 0; b < n_blocks; b++) {
        u64 phys = fs_ext4_logical_to_physical(ctx, dir_ino, b);
        if (phys == 0) continue;
        if (fs_ext4_read_block(ctx, phys, buf) != 0) { kfree(buf); return -1; }
        u32 off = 0;
        /* BUG-0062 FIX: every field read here is bounded by the block:
         * the fixed 8-byte dirent header must fit, rec_len must be
         * positive and stay inside the block, and the name must fit
         * inside the record. Crafted volumes previously caused the
         * strncmp to walk past the end of the block buffer. */
        while (off + 8 <= ctx->block_size) {
            fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)(buf + off);
            if (de->rec_len < 8 || (u32)de->rec_len > ctx->block_size - off)
                break;
            if (de->inode != 0 &&
                de->name_len <= (u8)(de->rec_len - 8) &&
                de->name_len == (u8)name_len) {
                if (strncmp((const char *)de->name, name,
                               (usize)name_len) == 0) {
                    *out_inode = de->inode;
                    kfree(buf);
                    return 0;
                }
            }
            off += de->rec_len;
        }
    }
    kfree(buf);
    return -1;
}

/* Allocate a VFS node for the given inode number. Reads the inode from
 * disk, builds the fs_ext4_inode_t private, and returns the new node. */
static fs_vfs_node_t *fs_ext4_node_from_inode(fs_ext4_ctx_t *ctx, u32 inode_num,
                                        const char *name) {
    fs_ext4_raw_inode_t raw;
    if (fs_ext4_read_inode(ctx, inode_num, &raw) != 0) return NULL;
    int is_dir = ((raw.i_mode & EXT4_S_IFMT) == EXT4_S_IFDIR);
    fs_ext4_inode_t *ino = (fs_ext4_inode_t *)kmalloc(sizeof(fs_ext4_inode_t));
    if (!ino) return NULL;
    memset(ino, 0, sizeof(*ino));
    ino->ctx = ctx;
    ino->inode_num = inode_num;
    ino->file_size = raw.i_size_lo | ((u64)raw.i_size_hi << 32);
    ino->i_flags = raw.i_flags;
    memcpy(ino->i_block, raw.i_block, sizeof(ino->i_block));
    fs_vfs_node_t *cn = fs_vfs_alloc_node(name,
                                    is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE,
                                    &g_ext4_fs_type);
    if (!cn) { kfree(ino); return NULL; }
    cn->private = ino;
    cn->size = ino->file_size;
    return cn;
}

/* ---- VFS file operations ---- */

static int fs_ext4_open(fs_vfs_node_t *node, int flags) {
    if (!node) return -1;
    if (node->type == VFS_TYPE_DIR) return -2;
    /* ext4 is read-only; reject write/create flags. */
    if (flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT)) return -3;
    return 0;
}

static int fs_ext4_read(fs_vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size <= 0) return -1;
    fs_ext4_inode_t *ino = (fs_ext4_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    fs_ext4_ctx_t *ctx = ino->ctx;
    u64 file_size = node->size;
    if (offset >= file_size) return 0;
    int avail = (int)(file_size - offset);
    if (size > avail) size = avail;
    int total = 0;
    u8 *dst = (u8 *)buf;
    u8 *bbuf = (u8 *)kmalloc(ctx->block_size);
    if (!bbuf) return -3;
    u64 pos = offset;
    while (total < size) {
        u32 logical = (u32)(pos / ctx->block_size);
        u32 off_in_block = (u32)(pos % ctx->block_size);
        u32 bytes_this = ctx->block_size - off_in_block;
        if (bytes_this > (u32)(size - total)) bytes_this = (u32)(size - total);
        u64 phys = fs_ext4_logical_to_physical(ctx, ino, logical);
        if (phys == 0) {
            /* Unmapped block (sparse file) - return zeros. */
            memset(dst + total, 0, bytes_this);
        } else {
            if (fs_ext4_read_block(ctx, phys, bbuf) != 0) break;
            memcpy(dst + total, bbuf + off_in_block, bytes_this);
        }
        pos += bytes_this;
        total += (int)bytes_this;
    }
    kfree(bbuf);
    return total;
}

static int fs_ext4_write(fs_vfs_node_t *node, u64 offset, const void *buf, int size) {
    (void)node; (void)offset; (void)buf; (void)size;
    return -1;  /* read-only */
}

static u64 fs_ext4_seek(fs_vfs_node_t *node, u64 offset, int whence) {
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

static int fs_ext4_close(fs_vfs_node_t *node) {
    (void)node;
    return 0;
}

static int fs_ext4_stat(fs_vfs_node_t *node, fs_vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- VFS directory operations ---- */

static int fs_ext4_mkdir(fs_vfs_node_t *parent, const char *name) {
    (void)parent; (void)name;
    return -1;  /* read-only */
}

static int fs_ext4_rmdir(fs_vfs_node_t *parent, const char *name) {
    (void)parent; (void)name;
    return -1;  /* read-only */
}

static int fs_ext4_readdir(fs_vfs_node_t *dir, int index, fs_vfs_dirent_t *entry) {
    if (!dir || !entry || index < 0) return -1;
    fs_ext4_inode_t *ino = (fs_ext4_inode_t *)dir->private;
    if (!ino || !ino->ctx) return -2;
    fs_ext4_ctx_t *ctx = ino->ctx;
    u32 n_blocks = (u32)(ino->file_size / ctx->block_size);
    if (n_blocks == 0) n_blocks = 1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -3;
    int cur_idx = 0;
    int result = -4;
    for (u32 b = 0; b < n_blocks && result != 0; b++) {
        u64 phys = fs_ext4_logical_to_physical(ctx, ino, b);
        if (phys == 0) continue;
        if (fs_ext4_read_block(ctx, phys, buf) != 0) { kfree(buf); return -5; }
        u32 off = 0;
        while (off < ctx->block_size) {
            fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)(buf + off);
            if (de->rec_len == 0) break;
            /* P2-13 FIX: skip "ghost" entries — inode 0 (deleted/unused slot)
             * OR empty name (name_len == 0, sometimes written by buggy
             * mkfs / fsck utilities as padding). Without this check, an
             * empty-name entry would surface in `ls` as a blank line and
             * an inode-0 entry would yield a bogus "inode 0" dirent. */
            if (de->inode != 0 && de->name_len != 0) {
                int skip = 0;
                if (de->name_len == 1 && de->name[0] == '.') skip = 1;
                else if (de->name_len == 2 && de->name[0] == '.' &&
                         de->name[1] == '.') skip = 1;
                if (!skip) {
                    if (cur_idx == index) {
                        int nl = de->name_len;
                        if (nl >= (int)sizeof(entry->name))
                            nl = (int)sizeof(entry->name) - 1;
                        memcpy(entry->name, de->name, (usize)nl);
                        entry->name[nl] = 0;
                        /* Read child inode to determine type. */
                        fs_ext4_raw_inode_t cinode;
                        if (fs_ext4_read_inode(ctx, de->inode, &cinode) == 0) {
                            entry->type =
                                ((cinode.i_mode & EXT4_S_IFMT) == EXT4_S_IFDIR)
                                    ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                        } else {
                            entry->type = VFS_TYPE_FILE;
                        }
                        entry->inode = de->inode;
                        result = 0;
                        break;
                    }
                    cur_idx++;
                }
            }
            off += de->rec_len;
        }
    }
    kfree(buf);
    return result;
}

static fs_vfs_node_t *fs_ext4_lookup(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    fs_ext4_inode_t *pino = (fs_ext4_inode_t *)parent->private;
    if (!pino || !pino->ctx) return NULL;
    fs_ext4_ctx_t *ctx = pino->ctx;
    u32 child_inode;
    if (fs_ext4_find_in_dir(ctx, pino, name, &child_inode) != 0) return NULL;
    return fs_ext4_node_from_inode(ctx, child_inode, name);
}

static int fs_ext4_unlink(fs_vfs_node_t *parent, const char *name) {
    (void)parent; (void)name;
    return -1;  /* read-only */
}

static int fs_ext4_rename(fs_vfs_node_t *parent, const char *oldname,
                       const char *newname) {
    (void)parent; (void)oldname; (void)newname;
    return -1;  /* read-only */
}

/* ---- Mount / unmount ---- */

static fs_vfs_node_t *fs_ext4_fs_mount(const char *device) {
    int drive = fs_ext4_parse_device(device);
    if (drive < 0) {
        screen_console_puts("ext4: invalid device string\n");
        return NULL;
    }
    if (!driver_block_ata_detect(drive)) {
        screen_console_puts("ext4: drive not present\n");
        return NULL;
    }
    fs_ext4_ctx_t *ctx = (fs_ext4_ctx_t *)kmalloc(sizeof(fs_ext4_ctx_t));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->drive = drive;

    fs_ext4_sb_t sb;
    if (fs_ext4_load_sb(ctx, &sb) != 0) {
        screen_console_puts("ext4: read superblock failed\n");
        kfree(ctx);
        return NULL;
    }
    if (sb.s_magic != EXT4_MAGIC) {
        screen_console_puts("ext4: not an ext4 volume (bad magic)\n");
        kfree(ctx);
        return NULL;
    }
    /* P0fix1 BUG-0003 (A12-003): blocks_per_group / inodes_per_group come
     * straight off the disk and are used as divisors below (and at the
     * inode lookup). A crafted/corrupted volume with a zero value caused
     * a #DE divide error that halted the kernel during `mount`. */
    if (sb.s_blocks_per_group == 0 || sb.s_inodes_per_group == 0) {
        screen_console_puts("ext4: invalid superblock (zero group size)\n");
        kfree(ctx);
        return NULL;
    }
    if (sb.s_log_block_size > 6) {
        screen_console_puts("ext4: invalid block size\n");
        kfree(ctx);
        return NULL;
    }
    ctx->block_size = (u32)1024 << sb.s_log_block_size;
    ctx->inodes_count = sb.s_inodes_count;
    ctx->blocks_count = sb.s_blocks_count_lo;
    ctx->inodes_per_group = sb.s_inodes_per_group;
    ctx->blocks_per_group = sb.s_blocks_per_group;
    ctx->inode_size = sb.s_inode_size;
    ctx->desc_size = sb.s_desc_size;
    if (ctx->inode_size == 0) ctx->inode_size = 128;
    ctx->groups_count = (ctx->blocks_count + ctx->blocks_per_group - 1) /
                        ctx->blocks_per_group;

    /* Build the root node from inode #2. */
    fs_vfs_node_t *root = fs_ext4_node_from_inode(ctx, EXT4_ROOT_INO, "");
    if (!root) {
        screen_console_puts("ext4: read root inode failed\n");
        kfree(ctx);
        return NULL;
    }
    /* The root inode's ctx is set by fs_ext4_node_from_inode, but it has no
     * pointer back to ctx for unmount. The unmount walks node->private->ctx
     * so this is fine. */
    return root;
}

static int fs_ext4_fs_unmount(fs_vfs_node_t *root) {
    if (!root) return -1;
    fs_ext4_inode_t *ino = (fs_ext4_inode_t *)root->private;
    if (ino) {
        if (ino->ctx) kfree(ino->ctx);
        kfree(ino);
    }
    kfree(root);
    return 0;
}

/* ---- Init ---- */

void fs_ext4_init(void) {
    memset(&g_ext4_fs_type, 0, sizeof(g_ext4_fs_type));
    strncpy(g_ext4_fs_type.name, "ext4", sizeof(g_ext4_fs_type.name) - 1);
    g_ext4_fs_ops.mount   = fs_ext4_fs_mount;
    g_ext4_fs_ops.unmount = fs_ext4_fs_unmount;
    g_ext4_file_ops.open  = fs_ext4_open;
    g_ext4_file_ops.read  = fs_ext4_read;
    g_ext4_file_ops.write = fs_ext4_write;
    g_ext4_file_ops.seek  = fs_ext4_seek;
    g_ext4_file_ops.close = fs_ext4_close;
    g_ext4_file_ops.stat  = fs_ext4_stat;
    g_ext4_dir_ops.mkdir   = fs_ext4_mkdir;
    g_ext4_dir_ops.rmdir   = fs_ext4_rmdir;
    g_ext4_dir_ops.readdir = fs_ext4_readdir;
    g_ext4_dir_ops.lookup  = fs_ext4_lookup;
    g_ext4_dir_ops.unlink  = fs_ext4_unlink;
    g_ext4_dir_ops.rename  = fs_ext4_rename;
    g_ext4_fs_type.fs_ops   = &g_ext4_fs_ops;
    g_ext4_fs_type.file_ops = &g_ext4_file_ops;
    g_ext4_fs_type.dir_ops  = &g_ext4_dir_ops;

    fs_vfs_register_fs("ext4", &g_ext4_fs_ops, &g_ext4_file_ops,
                    &g_ext4_dir_ops);
    screen_console_puts("ext4: registered (read-only)\n");
}
