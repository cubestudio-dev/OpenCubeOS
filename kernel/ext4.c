/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/ext4.c
 * Purpose: ext4 filesystem driver (READ-ONLY) for the VFS.
 *
 * Uses the ORIGINAL WP-05 VFS interface (3 separate ops structs:
 * vfs_fs_ops_t, vfs_file_ops_t, vfs_dir_ops_t). The fs-wide state lives
 * in an ext4_ctx_t stored off the root node's private (via the root
 * inode). Each looked-up node carries an ext4_inode_t in its private.
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
 * except ext4_init. Memory comes from kmalloc/kfree; sector I/O goes
 * through ata_read_sectors (drive 0..3, LBA 0-based).
 *
 * The `device` string passed to mount is parsed into a drive number 0..3
 * (e.g. "ata0" -> 0). ext4 is mounted on the whole disk starting at LBA 0
 * (no partition offset). The superblock is read from byte 1024 (sector 2).
 */
#include "ext4.h"
#include "types.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "vfs.h"
#include "ata.h"

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
} ext4_sb_t;

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
} ext4_group_desc_t;

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
} ext4_raw_inode_t;

typedef struct {
    u16 eh_magic;
    u16 eh_entries;
    u16 eh_max;
    u16 eh_depth;
    u32 eh_generation;
} ext4_extent_header_t;

typedef struct {
    u32 ee_block;
    u16 ee_len;
    u16 ee_start_hi;
    u32 ee_start_lo;
} ext4_extent_t;

typedef struct {
    u32 ei_block;
    u16 ei_leaf_lo;
    u16 ei_leaf_hi;
    u32 ei_unused;
} ext4_extent_idx_t;

typedef struct {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    u8  name[255];
} ext4_dirent_t;
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
} ext4_ctx_t;

/* Per-node state (file or directory). Lives in vfs_node_t->private. */
typedef struct {
    ext4_ctx_t *ctx;
    u32  inode_num;
    u64  file_size;
    u32  i_flags;
    u32  i_block[15];
} ext4_inode_t;

static vfs_fs_type_t  g_ext4_fs_type;
static vfs_fs_ops_t   g_ext4_fs_ops;
static vfs_file_ops_t g_ext4_file_ops;
static vfs_dir_ops_t  g_ext4_dir_ops;

/* ---- Device string parsing ---- */

static int ext4_parse_device(const char *device) {
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

/* ---- Sector / block I/O ---- */

static int ext4_read_block(ext4_ctx_t *ctx, u64 block, void *buf) {
    u32 sectors_per_block = ctx->block_size / 512;
    u64 lba = block * sectors_per_block;
    int rc = ata_read_sectors(ctx->drive, lba, (int)sectors_per_block, buf);
    return rc == (int)sectors_per_block ? 0 : -1;
}

/* ---- Superblock + group descriptors ---- */

static int ext4_load_sb(ext4_ctx_t *ctx, ext4_sb_t *sb) {
    /* The superblock lives at byte 1024 (sector 2). It is 1024 bytes
     * wide on disk; our struct is ~364 bytes so reading 1 sector (512
     * bytes) covers it. We read 2 sectors to be safe. */
    u8 buf[1024];
    int rc = ata_read_sectors(ctx->drive, 2, 2, buf);
    if (rc != 2) return -1;
    oc_memcpy(sb, buf, sizeof(*sb));
    return 0;
}

static int ext4_load_group_desc(ext4_ctx_t *ctx, u32 group,
                                ext4_group_desc_t *gd) {
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
    int rc = ata_read_sectors(ctx->drive, desc_sector, 1, buf);
    if (rc != 1) return -1;
    oc_memcpy(gd, buf + off_in_sec, desc_size);
    return 0;
}

/* Read an inode by number into the caller-provided struct. */
static int ext4_read_inode(ext4_ctx_t *ctx, u32 inode_num,
                           ext4_raw_inode_t *inode) {
    if (inode_num == 0) return -1;
    u32 group = (inode_num - 1) / ctx->inodes_per_group;
    u32 index = (inode_num - 1) % ctx->inodes_per_group;
    ext4_group_desc_t gd;
    if (ext4_load_group_desc(ctx, group, &gd) != 0) return -1;
    u64 inode_table_block = gd.bg_inode_table_lo;
    if (ctx->desc_size >= 64) {
        inode_table_block |= (u64)gd.bg_inode_table_hi << 32;
    }
    u64 byte_offset = inode_table_block * ctx->block_size +
                      (u64)index * ctx->inode_size;
    u64 block = byte_offset / ctx->block_size;
    u32 off_in_block = (u32)(byte_offset % ctx->block_size);
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    if (ext4_read_block(ctx, block, buf) != 0) { kfree(buf); return -1; }
    oc_memcpy(inode, buf + off_in_block, sizeof(*inode));
    kfree(buf);
    return 0;
}

/* ---- Block mapping ---- */

/* Map a logical block index to a physical block number via the extent
 * tree starting at the inode's i_block[0..] (root header). Returns
 * physical block, or 0 if unmapped. */
static u64 ext4_extent_lookup(ext4_ctx_t *ctx, const u32 *i_block,
                              u32 logical) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)i_block;
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
        const ext4_extent_t *ext = (const ext4_extent_t *)(i_block + 4);
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
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(i_block + 4);
    for (u16 i = 0; i < n_entries; i++) {
        if (logical >= idx[i].ei_block) {
            if (i + 1 < hdr->eh_entries && logical >= idx[i + 1].ei_block)
                continue;
            u64 child = (u64)idx[i].ei_leaf_lo |
                        ((u64)idx[i].ei_leaf_hi << 32);
            u8 *child_buf = (u8 *)kmalloc(ctx->block_size);
            if (!child_buf) return 0;
            if (ext4_read_block(ctx, child, child_buf) != 0) {
                kfree(child_buf);
                return 0;
            }
            const ext4_extent_header_t *ch =
                (const ext4_extent_header_t *)child_buf;
            if (ch->eh_magic != EXT4_EXT_MAGIC) {
                kfree(child_buf);
                return 0;
            }
            const ext4_extent_t *ext =
                (const ext4_extent_t *)(child_buf + 8);
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
static u64 ext4_legacy_lookup(ext4_ctx_t *ctx, const u32 *i_block,
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
        if (ext4_read_block(ctx, ind_block, buf) != 0) { kfree(buf); return 0; }
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
        if (ext4_read_block(ctx, dind_block, buf) != 0) { kfree(buf); return 0; }
        u32 ind = buf[off / ptrs_per_block];
        kfree(buf);
        if (ind == 0) return 0;
        buf = (u32 *)kmalloc(ctx->block_size);
        if (!buf) return 0;
        if (ext4_read_block(ctx, ind, buf) != 0) { kfree(buf); return 0; }
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
    if (ext4_read_block(ctx, tind_block, buf) != 0) { kfree(buf); return 0; }
    u32 dind = buf[off / (ptrs_per_block * ptrs_per_block)];
    kfree(buf);
    if (dind == 0) return 0;
    off %= ptrs_per_block * ptrs_per_block;
    buf = (u32 *)kmalloc(ctx->block_size);
    if (!buf) return 0;
    if (ext4_read_block(ctx, dind, buf) != 0) { kfree(buf); return 0; }
    u32 ind = buf[off / ptrs_per_block];
    kfree(buf);
    if (ind == 0) return 0;
    buf = (u32 *)kmalloc(ctx->block_size);
    if (!buf) return 0;
    if (ext4_read_block(ctx, ind, buf) != 0) { kfree(buf); return 0; }
    u32 r = buf[off % ptrs_per_block];
    kfree(buf);
    return r;
}

static u64 ext4_logical_to_physical(ext4_ctx_t *ctx, ext4_inode_t *ino,
                                    u32 logical) {
    if (ino->i_flags & EXT4_EXTENTS_FLAG) {
        return ext4_extent_lookup(ctx, ino->i_block, logical);
    }
    return ext4_legacy_lookup(ctx, ino->i_block, logical);
}

/* ---- Directory iteration ---- */

/* Find a directory entry named `name` inside the directory described by
 * `dir_ino`. On success returns 0 and fills *out_inode with the child
 * inode number. Returns -1 on error or not found. */
static int ext4_find_in_dir(ext4_ctx_t *ctx, ext4_inode_t *dir_ino,
                            const char *name, u32 *out_inode) {
    int name_len = (int)oc_strlen(name);
    u32 n_blocks = (u32)(dir_ino->file_size / ctx->block_size);
    if (n_blocks == 0) n_blocks = 1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    for (u32 b = 0; b < n_blocks; b++) {
        u64 phys = ext4_logical_to_physical(ctx, dir_ino, b);
        if (phys == 0) continue;
        if (ext4_read_block(ctx, phys, buf) != 0) { kfree(buf); return -1; }
        u32 off = 0;
        while (off < ctx->block_size) {
            ext4_dirent_t *de = (ext4_dirent_t *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == (u8)name_len) {
                if (oc_strncmp((const char *)de->name, name,
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
 * disk, builds the ext4_inode_t private, and returns the new node. */
static vfs_node_t *ext4_node_from_inode(ext4_ctx_t *ctx, u32 inode_num,
                                        const char *name) {
    ext4_raw_inode_t raw;
    if (ext4_read_inode(ctx, inode_num, &raw) != 0) return NULL;
    int is_dir = ((raw.i_mode & EXT4_S_IFMT) == EXT4_S_IFDIR);
    ext4_inode_t *ino = (ext4_inode_t *)kmalloc(sizeof(ext4_inode_t));
    if (!ino) return NULL;
    oc_memset(ino, 0, sizeof(*ino));
    ino->ctx = ctx;
    ino->inode_num = inode_num;
    ino->file_size = raw.i_size_lo | ((u64)raw.i_size_hi << 32);
    ino->i_flags = raw.i_flags;
    oc_memcpy(ino->i_block, raw.i_block, sizeof(ino->i_block));
    vfs_node_t *cn = vfs_alloc_node(name,
                                    is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE,
                                    &g_ext4_fs_type);
    if (!cn) { kfree(ino); return NULL; }
    cn->private = ino;
    cn->size = ino->file_size;
    return cn;
}

/* ---- VFS file operations ---- */

static int ext4_open(vfs_node_t *node, int flags) {
    if (!node) return -1;
    if (node->type == VFS_TYPE_DIR) return -2;
    /* ext4 is read-only; reject write/create flags. */
    if (flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT)) return -3;
    return 0;
}

static int ext4_read(vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size <= 0) return -1;
    ext4_inode_t *ino = (ext4_inode_t *)node->private;
    if (!ino || !ino->ctx) return -2;
    ext4_ctx_t *ctx = ino->ctx;
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
        u64 phys = ext4_logical_to_physical(ctx, ino, logical);
        if (phys == 0) {
            /* Unmapped block (sparse file) - return zeros. */
            oc_memset(dst + total, 0, bytes_this);
        } else {
            if (ext4_read_block(ctx, phys, bbuf) != 0) break;
            oc_memcpy(dst + total, bbuf + off_in_block, bytes_this);
        }
        pos += bytes_this;
        total += (int)bytes_this;
    }
    kfree(bbuf);
    return total;
}

static int ext4_write(vfs_node_t *node, u64 offset, const void *buf, int size) {
    (void)node; (void)offset; (void)buf; (void)size;
    return -1;  /* read-only */
}

static u64 ext4_seek(vfs_node_t *node, u64 offset, int whence) {
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

static int ext4_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int ext4_stat(vfs_node_t *node, vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    oc_strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- VFS directory operations ---- */

static int ext4_mkdir(vfs_node_t *parent, const char *name) {
    (void)parent; (void)name;
    return -1;  /* read-only */
}

static int ext4_rmdir(vfs_node_t *parent, const char *name) {
    (void)parent; (void)name;
    return -1;  /* read-only */
}

static int ext4_readdir(vfs_node_t *dir, int index, vfs_dirent_t *entry) {
    if (!dir || !entry || index < 0) return -1;
    ext4_inode_t *ino = (ext4_inode_t *)dir->private;
    if (!ino || !ino->ctx) return -2;
    ext4_ctx_t *ctx = ino->ctx;
    u32 n_blocks = (u32)(ino->file_size / ctx->block_size);
    if (n_blocks == 0) n_blocks = 1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -3;
    int cur_idx = 0;
    int result = -4;
    for (u32 b = 0; b < n_blocks && result != 0; b++) {
        u64 phys = ext4_logical_to_physical(ctx, ino, b);
        if (phys == 0) continue;
        if (ext4_read_block(ctx, phys, buf) != 0) { kfree(buf); return -5; }
        u32 off = 0;
        while (off < ctx->block_size) {
            ext4_dirent_t *de = (ext4_dirent_t *)(buf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0) {
                int skip = 0;
                if (de->name_len == 1 && de->name[0] == '.') skip = 1;
                else if (de->name_len == 2 && de->name[0] == '.' &&
                         de->name[1] == '.') skip = 1;
                if (!skip) {
                    if (cur_idx == index) {
                        int nl = de->name_len;
                        if (nl >= (int)sizeof(entry->name))
                            nl = (int)sizeof(entry->name) - 1;
                        oc_memcpy(entry->name, de->name, (usize)nl);
                        entry->name[nl] = 0;
                        /* Read child inode to determine type. */
                        ext4_raw_inode_t cinode;
                        if (ext4_read_inode(ctx, de->inode, &cinode) == 0) {
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

static vfs_node_t *ext4_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    ext4_inode_t *pino = (ext4_inode_t *)parent->private;
    if (!pino || !pino->ctx) return NULL;
    ext4_ctx_t *ctx = pino->ctx;
    u32 child_inode;
    if (ext4_find_in_dir(ctx, pino, name, &child_inode) != 0) return NULL;
    return ext4_node_from_inode(ctx, child_inode, name);
}

static int ext4_unlink(vfs_node_t *parent, const char *name) {
    (void)parent; (void)name;
    return -1;  /* read-only */
}

static int ext4_rename(vfs_node_t *parent, const char *oldname,
                       const char *newname) {
    (void)parent; (void)oldname; (void)newname;
    return -1;  /* read-only */
}

/* ---- Mount / unmount ---- */

static vfs_node_t *ext4_fs_mount(const char *device) {
    int drive = ext4_parse_device(device);
    if (drive < 0) {
        oc_console_puts("ext4: invalid device string\n");
        return NULL;
    }
    if (!ata_detect(drive)) {
        oc_console_puts("ext4: drive not present\n");
        return NULL;
    }
    ext4_ctx_t *ctx = (ext4_ctx_t *)kmalloc(sizeof(ext4_ctx_t));
    if (!ctx) return NULL;
    oc_memset(ctx, 0, sizeof(*ctx));
    ctx->drive = drive;

    ext4_sb_t sb;
    if (ext4_load_sb(ctx, &sb) != 0) {
        oc_console_puts("ext4: read superblock failed\n");
        kfree(ctx);
        return NULL;
    }
    if (sb.s_magic != EXT4_MAGIC) {
        oc_console_puts("ext4: not an ext4 volume (bad magic)\n");
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
    vfs_node_t *root = ext4_node_from_inode(ctx, EXT4_ROOT_INO, "");
    if (!root) {
        oc_console_puts("ext4: read root inode failed\n");
        kfree(ctx);
        return NULL;
    }
    /* The root inode's ctx is set by ext4_node_from_inode, but it has no
     * pointer back to ctx for unmount. The unmount walks node->private->ctx
     * so this is fine. */
    return root;
}

static int ext4_fs_unmount(vfs_node_t *root) {
    if (!root) return -1;
    ext4_inode_t *ino = (ext4_inode_t *)root->private;
    if (ino) {
        if (ino->ctx) kfree(ino->ctx);
        kfree(ino);
    }
    kfree(root);
    return 0;
}

/* ---- Init ---- */

void ext4_init(void) {
    oc_memset(&g_ext4_fs_type, 0, sizeof(g_ext4_fs_type));
    oc_strncpy(g_ext4_fs_type.name, "ext4", sizeof(g_ext4_fs_type.name) - 1);
    g_ext4_fs_ops.mount   = ext4_fs_mount;
    g_ext4_fs_ops.unmount = ext4_fs_unmount;
    g_ext4_file_ops.open  = ext4_open;
    g_ext4_file_ops.read  = ext4_read;
    g_ext4_file_ops.write = ext4_write;
    g_ext4_file_ops.seek  = ext4_seek;
    g_ext4_file_ops.close = ext4_close;
    g_ext4_file_ops.stat  = ext4_stat;
    g_ext4_dir_ops.mkdir   = ext4_mkdir;
    g_ext4_dir_ops.rmdir   = ext4_rmdir;
    g_ext4_dir_ops.readdir = ext4_readdir;
    g_ext4_dir_ops.lookup  = ext4_lookup;
    g_ext4_dir_ops.unlink  = ext4_unlink;
    g_ext4_dir_ops.rename  = ext4_rename;
    g_ext4_fs_type.fs_ops   = &g_ext4_fs_ops;
    g_ext4_fs_type.file_ops = &g_ext4_file_ops;
    g_ext4_fs_type.dir_ops  = &g_ext4_dir_ops;

    vfs_register_fs("ext4", &g_ext4_fs_ops, &g_ext4_file_ops,
                    &g_ext4_dir_ops);
    oc_console_puts("ext4: registered (read-only)\n");
}
