/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: fs/fs_ext4.h
 * Purpose: ext4 filesystem driver — read-only support via VFS.
 */
#ifndef OC_EXT4_H
#define OC_EXT4_H

#include "types.h"

/* Register the ext4 driver with the VFS. */
void fs_ext4_init(void);


/* =========================================================
 * WP-06b-E13: shared ext4 structures (moved verbatim from
 * fs_ext4.c so ocse_ext4w.c - the write path - sees the same
 * layouts; the read driver keeps its kmalloc'd copies).
 * ========================================================= */
#include "fs_vfs.h"
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

/* FS-wide state. Lives in the root node's inode->ctx. */
typedef struct {
    int  dev_idx;                /* BUG-0197: blk-layer device index (any
                                    driver, any partition sub-device) */
    u32  block_size;
    u32  inodes_count;
    u64  blocks_count;           /* 64BIT volumes: lo | hi<<32 */
    u32  inodes_per_group;
    u32  blocks_per_group;
    u32  inode_size;
    u32  desc_size;
    u64  groups_count;
} fs_ext4_ctx_t;

/* Per-node state (file or directory). Lives in fs_vfs_node_t->private. */
typedef struct {
    fs_ext4_ctx_t *ctx;
    u32  inode_num;
    u64  file_size;
    u32  i_flags;
    u32  i_block[15];
} fs_ext4_inode_t;

/* WP-06b: the ext4 write path (ocse_ext4w.c). */
void ocse_ext4w_attach(fs_vfs_file_ops_t *file_ops,
                      fs_vfs_dir_ops_t *dir_ops);

#endif /* OC_EXT4_H */
