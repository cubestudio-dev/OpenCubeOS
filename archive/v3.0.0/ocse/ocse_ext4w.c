/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_ext4w.c
 * Purpose: the ext4 WRITE path (WP-06b). The vendored fs_ext4.c driver
 * is read-only (WP-07 scope); the installer needs a writable root
 * filesystem (extract OCOS.TAR, write /etc/opencube.conf), so this
 * module implements the mutating ops on the SAME on-disk structures
 * (fs_ext4.h, moved there by ocse_edits.py E13):
 *
 *   file_ops: open (allow write flags + O_TRUNC), write (grow + RMW)
 *   dir_ops:  create / mkdir / rmdir / unlink / rename
 *
 * On-disk profile this write path produces (matches ocse_mkfs.c):
 * 4 KiB blocks, no journal, extent trees (depth grown to 1 when the
 * 4-slot inode root overflows), 32-byte group descriptors, inode
 * size from the superblock, deterministic first-fit allocation
 * (group 0 upward, goal-adjacent runs) so installs are reproducible.
 *
 * Deliberate scope limits (documented, enforced with clean errors):
 *  - legacy (ext2/3 indirect-map) inodes are NOT writable: the
 *    installer only ever touches volumes created by ocse_mkfs_ext4().
 *  - a depth-1 leaf holds (BSZ-12)/12 entries; when it fills, the op
 *    fails with a clear "extent table full" instead of growing the
 *    tree further. With coalescing runs a 2 MB tar extract needs 1-2
 *    extents, so the limit is far beyond installer workloads.
 *  - rename of directories is refused (files only).
 */
#include "fs_ext4.h"
#include "types.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "driver_block_blk.h"
#include "screen_console.h"
#include <stdint.h>

#define E4W_EXT_MAGIC       0xF30A
#define E4W_IFMT            0xF000u
#define E4W_IFDIR           0x4000u
#define E4W_IFREG           0x8000u
#define E4W_EXTENTS_FLAG    0x80000u
#define E4W_ROOT_INO        2
#define E4W_MAX_RUN         64          /* blocks per allocation batch */
#define E4W_EE_MAX_LEN      32768       /* ee_len is u16 (spec cap) */
#define E4W_FT_REG          1           /* dirent file_type values */
#define E4W_FT_DIR          2

/* ==================================================================
 * Block / metadata I/O (all buffers kmalloc'd - no 4 KiB on stack)
 * ================================================================== */

static int e4w_rdblk(fs_ext4_ctx_t *ctx, u64 block, u8 *buf) {
    u32 spb = ctx->block_size / 512;
    return driver_block_read_sectors_raw(ctx->dev_idx, block * spb, spb, buf);
}

static int e4w_wrblk(fs_ext4_ctx_t *ctx, u64 block, const u8 *buf) {
    u32 spb = ctx->block_size / 512;
    return driver_block_write_sectors_raw(ctx->dev_idx, block * spb, spb, buf);
}

/* Read or write the superblock (1024 bytes at byte offset 1024 =
 * sectors 2..3) and patch the free counters. */
static int e4w_sb_adjust(fs_ext4_ctx_t *ctx, int blk_delta, int ino_delta) {
    u8 buf[1024];
    if (blk_delta == 0 && ino_delta == 0) return 0;
    if (driver_block_read_sectors_raw(ctx->dev_idx, 2, 2, buf) != 0)
        return -1;
    i64 fb = (i64)*(u32 *)(buf + 12) + blk_delta;
    i64 fi = (i64)*(u32 *)(buf + 16) + ino_delta;
    if (fb < 0) fb = 0;
    if (fi < 0) fi = 0;
    *(u32 *)(buf + 12) = (u32)fb;
    *(u32 *)(buf + 16) = (u32)fi;
    return driver_block_write_sectors_raw(ctx->dev_idx, 2, 2, buf);
}

static int e4w_gd_read(fs_ext4_ctx_t *ctx, u32 group,
                       fs_ext4_group_desc_t *gd) {
    u8 buf[512];
    u32 ds = ctx->desc_size ? ctx->desc_size : 32;
    if (ds > sizeof(*gd)) ds = (u32)sizeof(*gd);
    u64 byte_off = (u64)ctx->block_size + (u64)group * ds;
    u64 sec = byte_off / 512;
    u32 off = (u32)(byte_off % 512);
    if (off + ds > 512) return -1;
    if (driver_block_read_sectors_raw(ctx->dev_idx, sec, 1, buf) != 0)
        return -1;
    memcpy(gd, buf + off, ds);
    return 0;
}

static int e4w_gd_write(fs_ext4_ctx_t *ctx, u32 group,
                        const fs_ext4_group_desc_t *gd) {
    u8 buf[512];
    u32 ds = ctx->desc_size ? ctx->desc_size : 32;
    if (ds > sizeof(*gd)) ds = (u32)sizeof(*gd);
    u64 byte_off = (u64)ctx->block_size + (u64)group * ds;
    u64 sec = byte_off / 512;
    u32 off = (u32)(byte_off % 512);
    if (off + ds > 512) return -1;
    if (driver_block_read_sectors_raw(ctx->dev_idx, sec, 1, buf) != 0)
        return -1;
    memcpy(buf + off, gd, ds);
    return driver_block_write_sectors_raw(ctx->dev_idx, sec, 1, buf);
}

/* Adjust a group's free-block / free-inode / used-dir counters. */
static int e4w_gd_adjust(fs_ext4_ctx_t *ctx, u32 group, int blk_delta,
                         int ino_delta, int dirs_delta) {
    fs_ext4_group_desc_t gd;
    if (e4w_gd_read(ctx, group, &gd) != 0) return -1;
    if (blk_delta) {
        i64 v = (i64)gd.bg_free_blocks_count_lo + blk_delta;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        gd.bg_free_blocks_count_lo = (u16)v;
    }
    if (ino_delta) {
        i64 v = (i64)gd.bg_free_inodes_count_lo + ino_delta;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        gd.bg_free_inodes_count_lo = (u16)v;
    }
    if (dirs_delta) {
        i64 v = (i64)gd.bg_used_dirs_count_lo + dirs_delta;
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        gd.bg_used_dirs_count_lo = (u16)v;
    }
    return e4w_gd_write(ctx, group, &gd);
}

/* Read an inode (256-byte raw entry) by number. */
static int e4w_inode_read(fs_ext4_ctx_t *ctx, u32 num,
                          fs_ext4_raw_inode_t *raw) {
    if (num == 0 || ctx->inodes_per_group == 0) return -1;
    u32 group = (num - 1) / ctx->inodes_per_group;
    u32 index = (num - 1) % ctx->inodes_per_group;
    fs_ext4_group_desc_t gd;
    if (e4w_gd_read(ctx, group, &gd) != 0) return -1;
    u64 itbl = gd.bg_inode_table_lo;
    if (ctx->desc_size >= 64) itbl |= (u64)gd.bg_inode_table_hi << 32;
    u64 byte_off = itbl * ctx->block_size + (u64)index * ctx->inode_size;
    u64 blk = byte_off / ctx->block_size;
    u32 off = (u32)(byte_off % ctx->block_size);
    if (ctx->inode_size == 0 ||
        off + (u32)sizeof(*raw) > ctx->block_size) return -1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    int rc = e4w_rdblk(ctx, blk, buf);
    if (rc == 0) memcpy(raw, buf + off, sizeof(*raw));
    kfree(buf);
    return rc;
}

/* Write an inode back (read-modify-write of its containing block). */
static int e4w_inode_write(fs_ext4_ctx_t *ctx, u32 num,
                           const fs_ext4_raw_inode_t *raw) {
    if (num == 0 || ctx->inodes_per_group == 0) return -1;
    u32 group = (num - 1) / ctx->inodes_per_group;
    u32 index = (num - 1) % ctx->inodes_per_group;
    fs_ext4_group_desc_t gd;
    if (e4w_gd_read(ctx, group, &gd) != 0) return -1;
    u64 itbl = gd.bg_inode_table_lo;
    if (ctx->desc_size >= 64) itbl |= (u64)gd.bg_inode_table_hi << 32;
    u64 byte_off = itbl * ctx->block_size + (u64)index * ctx->inode_size;
    u64 blk = byte_off / ctx->block_size;
    u32 off = (u32)(byte_off % ctx->block_size);
    if (ctx->inode_size == 0 ||
        off + (u32)sizeof(*raw) > ctx->block_size) return -1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    int rc = e4w_rdblk(ctx, blk, buf);
    if (rc == 0) {
        memcpy(buf + off, raw, sizeof(*raw));
        rc = e4w_wrblk(ctx, blk, buf);
    }
    kfree(buf);
    return rc;
}

/* Push the cached node state (size / extent tree) into the on-disk
 * inode. add_blocks: 512-byte-sector units are computed here from the
 * block count the caller allocated since the last sync. */
static int e4w_inode_sync(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *ino,
                          u32 add_blocks) {
    fs_ext4_raw_inode_t raw;
    if (e4w_inode_read(ctx, ino->inode_num, &raw) != 0) return -1;
    raw.i_size_lo = (u32)ino->file_size;
    raw.i_size_hi = (u32)(ino->file_size >> 32);
    raw.i_flags = ino->i_flags;
    memcpy(raw.i_block, ino->i_block, sizeof(raw.i_block));
    raw.i_blocks_lo += add_blocks * (ctx->block_size / 512);
    return e4w_inode_write(ctx, ino->inode_num, &raw);
}

/* ==================================================================
 * Block allocator: deterministic first fit, group 0 upward, biased to
 * `goal` (the previous allocation's end) so sequential writes coalesce
 * into few extents.
 * ================================================================== */

static u32 e4w_group_blocks(fs_ext4_ctx_t *ctx, u32 g) {
    u64 base = (u64)g * ctx->blocks_per_group;
    if (base >= ctx->blocks_count) return 0;
    u64 rem = ctx->blocks_count - base;
    return rem > ctx->blocks_per_group ? ctx->blocks_per_group : (u32)rem;
}

static int e4w_bitmap_get(fs_ext4_ctx_t *ctx, u64 blk, u8 *buf) {
    return e4w_rdblk(ctx, blk, buf);
}

static int e4w_bitmap_put(fs_ext4_ctx_t *ctx, u64 blk, const u8 *buf) {
    return e4w_wrblk(ctx, blk, buf);
}

/* Allocate up to `want` contiguous free blocks from group `g`.
 * Returns the run length (>= 1) or 0 if the group has no free run. */
static u32 e4w_alloc_in_group(fs_ext4_ctx_t *ctx, u32 g, u32 want,
                              u64 goal, u64 *out_phys) {
    fs_ext4_group_desc_t gd;
    u32 gblocks = e4w_group_blocks(ctx, g);
    if (gblocks == 0) return 0;
    if (e4w_gd_read(ctx, g, &gd) != 0) return 0;
    if (gd.bg_free_blocks_count_lo == 0) return 0;
    u8 *bm = (u8 *)kmalloc(ctx->block_size);
    if (!bm) return 0;
    u64 bb = gd.bg_block_bitmap_lo;
    if (ctx->desc_size >= 64) bb |= (u64)gd.bg_block_bitmap_hi << 32;
    u32 got = 0;
    if (e4w_bitmap_get(ctx, bb, bm) == 0) {
        u32 start = 0;
        if (goal > (u64)g * ctx->blocks_per_group) {
            u64 gbit = goal - (u64)g * ctx->blocks_per_group;
            if (gbit < gblocks) start = (u32)gbit;
        }
        /* first pass from the goal, second wrap-around pass */
        for (u32 pass = 0; pass < 2 && got == 0; pass++) {
            u32 b0 = pass == 0 ? start : 0;
            for (u32 b = b0; b + 1 < gblocks && got == 0; b++) {
                if (bm[b >> 3] & (u8)(1u << (b & 7))) continue;
                u32 run = 0;
                while (b + run < gblocks && run < want &&
                       !(bm[(b + run) >> 3] & (u8)(1u << ((b + run) & 7))))
                    run++;
                if (run == 0) { b++; continue; }
                for (u32 i = 0; i < run; i++)
                    bm[(b + i) >> 3] |= (u8)(1u << ((b + i) & 7));
                if (e4w_bitmap_put(ctx, bb, bm) == 0) {
                    got = run;
                    *out_phys = (u64)g * ctx->blocks_per_group + b;
                }
            }
        }
    }
    kfree(bm);
    if (got) {
        e4w_gd_adjust(ctx, g, -(int)got, 0, 0);
        e4w_sb_adjust(ctx, -(int)got, 0);
    }
    return got;
}

/* Allocate up to `want` contiguous blocks scanning groups upward. */
static u32 e4w_alloc(fs_ext4_ctx_t *ctx, u32 want, u64 goal, u64 *phys) {
    u32 groups = (u32)ctx->groups_count;
    if (groups == 0) return 0;
    /* goal group first (sequential locality), then all groups in order */
    u32 gg = goal < ctx->blocks_count ? (u32)(goal / ctx->blocks_per_group) : 0;
    if (gg >= groups) gg = 0;
    u32 got = e4w_alloc_in_group(ctx, gg, want, goal, phys);
    if (got) return got;
    for (u32 g = 0; g < groups; g++) {
        if (g == gg) continue;
        got = e4w_alloc_in_group(ctx, g, want, 0, phys);
        if (got) return got;
    }
    return 0;
}

/* Free a run of blocks (splits at group boundaries). */
static int e4w_free(fs_ext4_ctx_t *ctx, u64 phys, u32 len) {
    while (len > 0) {
        u32 g = (u32)(phys / ctx->blocks_per_group);
        u32 gblocks = e4w_group_blocks(ctx, g);
        if (gblocks == 0) return -1;
        u64 base = (u64)g * ctx->blocks_per_group;
        u32 bit = (u32)(phys - base);
        u32 here = gblocks - bit;
        if (here > (u32)len) here = (u32)len;
        fs_ext4_group_desc_t gd;
        if (e4w_gd_read(ctx, g, &gd) != 0) return -1;
        u8 *bm = (u8 *)kmalloc(ctx->block_size);
        if (!bm) return -1;
        u64 bb = gd.bg_block_bitmap_lo;
        if (ctx->desc_size >= 64) bb |= (u64)gd.bg_block_bitmap_hi << 32;
        int rc = e4w_bitmap_get(ctx, bb, bm);
        if (rc == 0) {
            for (u32 i = 0; i < here; i++)
                bm[(bit + i) >> 3] &= (u8)~(1u << ((bit + i) & 7));
            rc = e4w_bitmap_put(ctx, bb, bm);
        }
        kfree(bm);
        if (rc != 0) return -1;
        e4w_gd_adjust(ctx, g, (int)here, 0, 0);
        e4w_sb_adjust(ctx, (int)here, 0);
        phys += here;
        len -= here;
    }
    return 0;
}

/* ==================================================================
 * Extent tree: lookup (same walk as the read driver) + map-with-grow
 * ================================================================== */

static u64 e4w_ext_lookup(fs_ext4_ctx_t *ctx, const u32 *i_block,
                          u32 logical) {
    const fs_ext4_extent_header_t *hdr =
        (const fs_ext4_extent_header_t *)i_block;
    if (hdr->eh_magic != E4W_EXT_MAGIC) return 0;
    if (hdr->eh_depth > 5) return 0;
    u8 *heap = NULL;
    u64 result = 0;
    int levels = 0;
    for (;;) {
        u16 max_ent = heap ? (u16)((ctx->block_size - 12) / 12) : 4;
        u16 n = hdr->eh_entries;
        if (n > max_ent) n = max_ent;
        if (hdr->eh_depth == 0) {
            const fs_ext4_extent_t *ex =
                (const fs_ext4_extent_t *)((const u8 *)hdr + 12);
            for (u16 i = 0; i < n; i++) {
                if (logical >= ex[i].ee_block &&
                    (u64)logical < (u64)ex[i].ee_block + ex[i].ee_len) {
                    result = (u64)ex[i].ee_start_lo |
                             ((u64)ex[i].ee_start_hi << 32);
                    result += logical - ex[i].ee_block;
                    break;
                }
            }
            kfree(heap);
            return result;
        }
        const fs_ext4_extent_idx_t *ix =
            (const fs_ext4_extent_idx_t *)((const u8 *)hdr + 12);
        int chosen = -1;
        for (u16 i = 0; i < n; i++)
            if (logical >= ix[i].ei_block) chosen = (int)i;
        if (chosen < 0) { kfree(heap); return 0; }
        u64 child = (u64)ix[chosen].ei_leaf_lo |
                    ((u64)ix[chosen].ei_leaf_hi << 32);
        u8 *nb = (u8 *)kmalloc(ctx->block_size);
        if (!nb) { kfree(heap); return 0; }
        if (e4w_rdblk(ctx, child, nb) != 0) { kfree(nb); kfree(heap); return 0; }
        kfree(heap);
        heap = nb;
        hdr = (const fs_ext4_extent_header_t *)heap;
        if (hdr->eh_magic != E4W_EXT_MAGIC || hdr->eh_depth > 5) {
            kfree(heap);
            return 0;
        }
        if (++levels > 5) { kfree(heap); return 0; }
    }
}

/* A leaf (or the inode root) opened for mutation. */
typedef struct {
    u8 *buf;                              /* NULL: root (in-inode) */
    fs_ext4_extent_header_t *hdr;
    u64 block_no;
} e4w_leaf_t;

static void e4w_leaf_put(fs_ext4_ctx_t *ctx, e4w_leaf_t *lf) {
    if (lf->buf) {
        e4w_wrblk(ctx, lf->block_no, lf->buf);
        kfree(lf->buf);
        lf->buf = NULL;
        lf->hdr = NULL;
    }
}

/* Descend from the root to the leaf that owns (or would own) `logical`.
 * The returned leaf is loaded for mutation; caller must e4w_leaf_put. */
static int e4w_leaf_open(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *ino,
                         u32 logical, e4w_leaf_t *lf) {
    lf->buf = NULL;
    lf->hdr = (fs_ext4_extent_header_t *)ino->i_block;
    lf->block_no = 0;
    fs_ext4_extent_header_t *hdr = lf->hdr;
    if (hdr->eh_magic != E4W_EXT_MAGIC) return -1;
    int levels = 0;
    while (hdr->eh_depth > 0) {
        u16 max_ent = lf->buf ? (u16)((ctx->block_size - 12) / 12) : 4;
        u16 n = hdr->eh_entries;
        if (n > max_ent) n = max_ent;
        fs_ext4_extent_idx_t *ix =
            (fs_ext4_extent_idx_t *)((u8 *)hdr + 12);
        int chosen = -1;
        for (u16 i = 0; i < n; i++)
            if (logical >= ix[i].ei_block) chosen = (int)i;
        if (chosen < 0) chosen = 0;       /* before the first subtree */
        u64 child = (u64)ix[chosen].ei_leaf_lo |
                    ((u64)ix[chosen].ei_leaf_hi << 32);
        u8 *nb = (u8 *)kmalloc(ctx->block_size);
        if (!nb) return -1;
        if (e4w_rdblk(ctx, child, nb) != 0) { kfree(nb); return -1; }
        /* flush the previous level (unchanged) before replacing */
        e4w_leaf_put(ctx, lf);
        lf->buf = nb;
        lf->block_no = child;
        lf->hdr = (fs_ext4_extent_header_t *)nb;
        hdr = lf->hdr;
        if (hdr->eh_magic != E4W_EXT_MAGIC) return -1;
        if (++levels > 5) return -1;
    }
    return 0;
}

/* Grow a full depth-0 root into a depth-1 tree: the 4 root extents
 * move into a fresh leaf block, the root becomes a 1-entry index. */
static int e4w_root_grow(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *ino) {
    fs_ext4_extent_header_t *root =
        (fs_ext4_extent_header_t *)ino->i_block;
    if (root->eh_magic != E4W_EXT_MAGIC || root->eh_depth != 0) return -1;
    u64 blk = 0;
    if (e4w_alloc(ctx, 1, 0, &blk) == 0) return -1;
    u8 *nb = (u8 *)kmalloc(ctx->block_size);
    if (!nb) { e4w_free(ctx, blk, 1); return -1; }
    memset(nb, 0, ctx->block_size);
    fs_ext4_extent_header_t *leaf = (fs_ext4_extent_header_t *)nb;
    leaf->eh_magic = E4W_EXT_MAGIC;
    leaf->eh_entries = root->eh_entries;
    leaf->eh_max = (u16)((ctx->block_size - 12) / 12);
    leaf->eh_depth = 0;
    leaf->eh_generation = 0;
    memcpy(nb + 12, (const u8 *)root + 12, (u64)root->eh_entries * 12);
    u32 first_logical = *(u32 *)((const u8 *)root + 12);
    if (e4w_wrblk(ctx, blk, nb) != 0) { kfree(nb); e4w_free(ctx, blk, 1); return -1; }
    kfree(nb);
    root->eh_entries = 1;
    /* eh_max stays 4 (the 60-byte inode root capacity) */
    root->eh_depth = 1;
    root->eh_generation = 0;
    fs_ext4_extent_idx_t *ix = (fs_ext4_extent_idx_t *)((u8 *)root + 12);
    memset(ix, 0, 12);
    ix->ei_block = first_logical;
    ix->ei_leaf_lo = (u32)blk;
    ix->ei_leaf_hi = (u16)(blk >> 32);
    return 0;
}

/* Ensure logical..logical+count-1 are mapped, allocating as needed.
 * Returns 0 on success (*out_alloc = number of blocks allocated) or
 * negative on failure (partial state is rolled back as far as the
 * tree allows; the caller stops writing on any error). */
static int e4w_ext_map(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *ino,
                       u32 logical, u32 count, u64 goal, u32 *out_alloc) {
    *out_alloc = 0;
    while (count > 0) {
        /* already covered? skip to the covering extent's end */
        u64 have = e4w_ext_lookup(ctx, ino->i_block, logical);
        if (have) {
            /* find the extent covering logical to learn its length */
            e4w_leaf_t lf;
            if (e4w_leaf_open(ctx, ino, logical, &lf) != 0) return -1;
            u16 max_ent = lf.buf ? (u16)((ctx->block_size - 12) / 12) : 4;
            u16 n = lf.hdr->eh_entries;
            if (n > max_ent) n = max_ent;
            fs_ext4_extent_t *ex = (fs_ext4_extent_t *)((u8 *)lf.hdr + 12);
            u32 skip = 1;
            for (u16 i = 0; i < n; i++) {
                if (logical >= ex[i].ee_block &&
                    (u64)logical < (u64)ex[i].ee_block + ex[i].ee_len) {
                    u32 end = ex[i].ee_block + ex[i].ee_len;
                    skip = end - logical;
                    break;
                }
            }
            e4w_leaf_put(ctx, &lf);
            if (skip >= count) return 0;
            logical += skip;
            count -= skip;
            continue;
        }
        /* allocate a run for the unmapped span */
        e4w_leaf_t lf;
        if (e4w_leaf_open(ctx, ino, logical, &lf) != 0) return -1;
        u16 max_ent = lf.buf ? (u16)((ctx->block_size - 12) / 12) : 4;
        u16 n = lf.hdr->eh_entries;
        if (n > max_ent) n = max_ent;
        fs_ext4_extent_t *ex = (fs_ext4_extent_t *)((u8 *)lf.hdr + 12);
        /* last extent with ee_block < logical (coalesce candidate) */
        int prev = -1;
        for (u16 i = 0; i < n; i++)
            if (ex[i].ee_block < logical) prev = (int)i;
        u64 phys = 0;
        u32 want = count > E4W_MAX_RUN ? E4W_MAX_RUN : count;
        u64 hint = 0;
        if (prev >= 0 && ex[prev].ee_block + ex[prev].ee_len == logical)
            hint = ((u64)ex[prev].ee_start_lo |
                    ((u64)ex[prev].ee_start_hi << 32)) + ex[prev].ee_len;
        else if (goal)
            hint = goal;
        u32 got = e4w_alloc(ctx, want, hint, &phys);
        if (got == 0) { e4w_leaf_put(ctx, &lf); return -1; }
        *out_alloc += got;
        if (prev >= 0 &&
            ex[prev].ee_block + ex[prev].ee_len == logical &&
            (u64)ex[prev].ee_start_lo + ((u64)ex[prev].ee_start_hi << 32) +
                ex[prev].ee_len == phys &&
            (u32)ex[prev].ee_len + got <= E4W_EE_MAX_LEN) {
            ex[prev].ee_len = (u16)(ex[prev].ee_len + got);
        } else {
            if (n >= max_ent) {
                /* leaf full: grow once (root only) and retry */
                if (!lf.buf && lf.hdr->eh_depth == 0) {
                    e4w_leaf_put(ctx, &lf);          /* root unchanged so far */
                    e4w_free(ctx, phys, got);        /* give the run back */
                    *out_alloc -= got;
                    if (e4w_root_grow(ctx, ino) != 0) return -1;
                    continue;                        /* retry this logical */
                }
                e4w_leaf_put(ctx, &lf);
                e4w_free(ctx, phys, got);
                *out_alloc -= got;
                screen_console_puts("ext4w: extent table full\n");
                return -1;
            }
            /* insert after prev (keeps ee_block ascending) */
            int at = prev + 1;
            for (u16 i = n; i > (u16)at; i--) ex[i] = ex[i - 1];
            ex[at].ee_block = logical;
            ex[at].ee_len = (u16)got;
            ex[at].ee_start_hi = (u16)(phys >> 32);
            ex[at].ee_start_lo = (u32)phys;
            lf.hdr->eh_entries = (u16)(n + 1);
        }
        e4w_leaf_put(ctx, &lf);
        logical += got;
        count -= (got >= count ? count : got);
    }
    return 0;
}

/* Recursively free every extent under a block-backed tree node, then
 * the node block itself. depth comes from the parent header and is
 * bounded by the 5-level spec limit the lookup walker also enforces. */
static int e4w_ext_free_node(fs_ext4_ctx_t *ctx, u64 block, u16 depth) {
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    if (e4w_rdblk(ctx, block, buf) == 0) {
        fs_ext4_extent_header_t *h = (fs_ext4_extent_header_t *)buf;
        if (h->eh_magic == E4W_EXT_MAGIC) {
            u16 cap = depth == 0 ? 4
                                 : (u16)((ctx->block_size - 12) / 12);
            u16 n = h->eh_entries;
            if (n > cap) n = cap;
            if (depth == 0) {
                fs_ext4_extent_t *ex = (fs_ext4_extent_t *)(buf + 12);
                for (u16 k = 0; k < n; k++)
                    e4w_free(ctx, (u64)ex[k].ee_start_lo |
                                      ((u64)ex[k].ee_start_hi << 32),
                             ex[k].ee_len ? ex[k].ee_len : 1);
            } else if (depth <= 5) {
                fs_ext4_extent_idx_t *ix =
                    (fs_ext4_extent_idx_t *)(buf + 12);
                for (u16 k = 0; k < n; k++) {
                    u64 child = (u64)ix[k].ei_leaf_lo |
                                ((u64)ix[k].ei_leaf_hi << 32);
                    e4w_ext_free_node(ctx, child, (u16)(depth - 1));
                }
            }
        }
    }
    kfree(buf);
    return e4w_free(ctx, block, 1);       /* the node block itself */
}

/* Walk every leaf extent and hand the blocks back (truncate / unlink). */
static int e4w_ext_free_all(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *ino) {
    fs_ext4_extent_header_t *hdr =
        (fs_ext4_extent_header_t *)ino->i_block;
    if (hdr->eh_magic != E4W_EXT_MAGIC) return -1;
    if (hdr->eh_depth == 0) {
        u16 n = hdr->eh_entries;
        if (n > 4) n = 4;
        fs_ext4_extent_t *ex = (fs_ext4_extent_t *)((u8 *)hdr + 12);
        for (u16 i = 0; i < n; i++)
            e4w_free(ctx, (u64)ex[i].ee_start_lo |
                              ((u64)ex[i].ee_start_hi << 32),
                     ex[i].ee_len ? ex[i].ee_len : 1);
    } else if (hdr->eh_depth <= 5) {
        u16 n = hdr->eh_entries;
        if (n > 4) n = 4;
        fs_ext4_extent_idx_t *ix =
            (fs_ext4_extent_idx_t *)((u8 *)hdr + 12);
        for (u16 i = 0; i < n; i++) {
            u64 child = (u64)ix[i].ei_leaf_lo |
                        ((u64)ix[i].ei_leaf_hi << 32);
            e4w_ext_free_node(ctx, child, (u16)(hdr->eh_depth - 1));
        }
    }
    hdr->eh_entries = 0;
    hdr->eh_depth = 0;
    return 0;
}

/* ==================================================================
 * Directory entry engine (linked-list dirent format, rec_len rules)
 * ================================================================== */

static u32 e4w_dirent_min(u32 name_len) {
    return (8 + name_len + 3) & ~3u;
}

typedef struct {
    u32 blk;            /* directory block index */
    u32 off;            /* entry offset inside the block */
    u32 prev_off;       /* previous entry offset (u32)-1 if none */
    u32 child_ino;
    u8  file_type;
} e4w_dirent_pos_t;

static int e4w_dir_scan(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *dir,
                        const char *name, e4w_dirent_pos_t *pos) {
    u32 nblocks = (u32)(dir->file_size / ctx->block_size);
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    int found = -1;
    for (u32 b = 0; b < nblocks && found != 0; b++) {
        u64 phys = e4w_ext_lookup(ctx, dir->i_block, b);
        if (!phys) continue;
        if (e4w_rdblk(ctx, phys, buf) != 0) { kfree(buf); return -1; }
        u32 off = 0;
        u32 prev = (u32)-1;
        while (off + 8 <= ctx->block_size) {
            fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)(buf + off);
            if (de->rec_len < 8 ||
                (u32)de->rec_len > ctx->block_size - off) break;
            if (de->inode != 0 && de->name_len == strlen(name) &&
                strncmp((const char *)de->name, name, de->name_len) == 0) {
                pos->blk = b;
                pos->off = off;
                pos->prev_off = prev;
                pos->child_ino = de->inode;
                pos->file_type = de->file_type;
                found = 0;
                break;
            }
            prev = off;
            off += de->rec_len;
        }
    }
    kfree(buf);
    return found;
}

/* Is the directory empty (only . / .. / deleted slots)? */
static int e4w_dir_empty(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *dir) {
    u32 nblocks = (u32)(dir->file_size / ctx->block_size);
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return 0;
    int empty = 1;
    for (u32 b = 0; b < nblocks && empty; b++) {
        u64 phys = e4w_ext_lookup(ctx, dir->i_block, b);
        if (!phys) continue;
        if (e4w_rdblk(ctx, phys, buf) != 0) break;
        u32 off = 0;
        while (off + 8 <= ctx->block_size) {
            fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)(buf + off);
            if (de->rec_len < 8 ||
                (u32)de->rec_len > ctx->block_size - off) break;
            if (de->inode != 0 && de->name_len != 0) {
                if (!(de->name_len == 1 && de->name[0] == '.') &&
                    !(de->name_len == 2 && de->name[0] == '.' &&
                      de->name[1] == '.')) {
                    empty = 0;
                    break;
                }
            }
            off += de->rec_len;
        }
    }
    kfree(buf);
    return empty;
}

/* Add an entry. Returns 0 or negative. The caller persists the parent
 * inode afterwards (size / extent tree may both change). */
static int e4w_dir_add(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *dir,
                       const char *name, u32 child, u8 ftype,
                       u32 *out_alloc) {
    u32 namelen = (u32)strlen(name);
    if (namelen == 0 || namelen > 255) return -1;
    u32 needed = e4w_dirent_min(namelen);
    *out_alloc = 0;
    u32 nblocks = (u32)(dir->file_size / ctx->block_size);
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    for (u32 b = 0; b < nblocks; b++) {
        u64 phys = e4w_ext_lookup(ctx, dir->i_block, b);
        if (!phys) continue;
        if (e4w_rdblk(ctx, phys, buf) != 0) { kfree(buf); return -1; }
        u32 off = 0;
        while (off + 8 <= ctx->block_size) {
            fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)(buf + off);
            if (de->rec_len < 8 ||
                (u32)de->rec_len > ctx->block_size - off) break;
            u32 is_last = (off + de->rec_len >= ctx->block_size);
            /* unused slot big enough for the name (+8 so a pad entry
             * with a valid header remains) */
            if (de->inode == 0 && de->rec_len >= needed + 8) {
                u32 old_rec = de->rec_len;
                de->rec_len = (u16)needed;
                de->inode = child;
                de->name_len = (u8)namelen;
                de->file_type = ftype;
                memcpy(de->name, name, namelen);
                fs_ext4_dirent_t *pad =
                    (fs_ext4_dirent_t *)(buf + off + needed);
                memset(pad, 0, 8);
                pad->rec_len = (u16)(old_rec - needed);
                if (e4w_wrblk(ctx, phys, buf) != 0) { kfree(buf); return -1; }
                kfree(buf);
                return 0;
            }
            /* last real entry: carve from its slack */
            if (is_last && de->inode != 0) {
                u32 min = e4w_dirent_min(de->name_len);
                if (de->rec_len - min >= needed) {
                    u32 old_rec = de->rec_len;
                    de->rec_len = (u16)min;
                    fs_ext4_dirent_t *ne =
                        (fs_ext4_dirent_t *)(buf + off + min);
                    memset(ne, 0, 8);
                    ne->rec_len = (u16)(old_rec - min);
                    ne->inode = child;
                    ne->name_len = (u8)namelen;
                    ne->file_type = ftype;
                    memcpy(ne->name, name, namelen);
                    if (e4w_wrblk(ctx, phys, buf) != 0) {
                        kfree(buf);
                        return -1;
                    }
                    kfree(buf);
                    return 0;
                }
            }
            off += de->rec_len;
        }
    }
    kfree(buf);
    /* append a fresh directory block: map first (ext_map allocates the
     * block AND records the extent), then fetch the physical block it
     * chose - never allocate twice (live-caught in the batch-3 review:
     * the first draft pre-allocated a block AND let ext_map allocate
     * another, writing the data to a block the extent did not cover) */
    u64 goal = 0;
    if (nblocks) {
        u64 prev = e4w_ext_lookup(ctx, dir->i_block, nblocks - 1);
        if (prev) goal = prev + 1;
    }
    u32 alloc = 0;
    if (e4w_ext_map(ctx, dir, nblocks, 1, goal, &alloc) != 0) return -1;
    u64 phys = e4w_ext_lookup(ctx, dir->i_block, nblocks);
    if (!phys) return -1;
    u8 *nb = (u8 *)kmalloc(ctx->block_size);
    if (!nb) return -1;
    memset(nb, 0, ctx->block_size);
    fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)nb;
    de->rec_len = (u16)ctx->block_size;
    de->inode = child;
    de->name_len = (u8)namelen;
    de->file_type = ftype;
    memcpy(de->name, name, namelen);
    int rc = e4w_wrblk(ctx, phys, nb);
    kfree(nb);
    if (rc != 0) return -1;
    dir->file_size += ctx->block_size;
    return 0;
}

/* Remove an entry by merging its record into the previous one (or
 * zeroing the inode when it is first in its block). */
static int e4w_dir_remove(fs_ext4_ctx_t *ctx, fs_ext4_inode_t *dir,
                          const char *name) {
    e4w_dirent_pos_t pos;
    if (e4w_dir_scan(ctx, dir, name, &pos) != 0) return -1;
    u64 phys = e4w_ext_lookup(ctx, dir->i_block, pos.blk);
    if (!phys) return -1;
    u8 *buf = (u8 *)kmalloc(ctx->block_size);
    if (!buf) return -1;
    if (e4w_rdblk(ctx, phys, buf) != 0) { kfree(buf); return -1; }
    fs_ext4_dirent_t *de = (fs_ext4_dirent_t *)(buf + pos.off);
    if (pos.prev_off != (u32)-1) {
        fs_ext4_dirent_t *prev =
            (fs_ext4_dirent_t *)(buf + pos.prev_off);
        prev->rec_len = (u16)(prev->rec_len + de->rec_len);
    } else {
        de->inode = 0;                /* first entry: ghost it */
    }
    int rc = e4w_wrblk(ctx, phys, buf);
    kfree(buf);
    return rc;
}

/* ==================================================================
 * Inode allocate / free
 * ================================================================== */

static int e4w_ino_alloc(fs_ext4_ctx_t *ctx, u32 *out) {
    u32 groups = (u32)ctx->groups_count;
    for (u32 g = 0; g < groups; g++) {
        fs_ext4_group_desc_t gd;
        if (e4w_gd_read(ctx, g, &gd) != 0) return -1;
        if (gd.bg_free_inodes_count_lo == 0) continue;
        u8 *bm = (u8 *)kmalloc(ctx->block_size);
        if (!bm) return -1;
        u64 ib = gd.bg_inode_bitmap_lo;
        if (ctx->desc_size >= 64) ib |= (u64)gd.bg_inode_bitmap_hi << 32;
        int rc = e4w_rdblk(ctx, ib, bm);
        if (rc != 0) { kfree(bm); return -1; }
        int found = -1;
        /* bits 0..9 are the reserved inodes 1..10 - never allocate */
        for (u32 b = 10; b < ctx->inodes_per_group; b++) {
            if (!(bm[b >> 3] & (u8)(1u << (b & 7)))) { found = (int)b; break; }
        }
        if (found < 0) { kfree(bm); continue; }
        bm[found >> 3] |= (u8)(1u << (found & 7));
        rc = e4w_wrblk(ctx, ib, bm);
        kfree(bm);
        if (rc != 0) return -1;
        e4w_gd_adjust(ctx, g, 0, -1, 0);
        e4w_sb_adjust(ctx, 0, -1);
        *out = g * ctx->inodes_per_group + (u32)found + 1;
        return 0;
    }
    return -1;
}

static int e4w_ino_free(fs_ext4_ctx_t *ctx, u32 num) {
    if (num < E4W_ROOT_INO) return -1;
    u32 group = (num - 1) / ctx->inodes_per_group;
    u32 bit = (num - 1) % ctx->inodes_per_group;
    fs_ext4_group_desc_t gd;
    if (e4w_gd_read(ctx, group, &gd) != 0) return -1;
    u8 *bm = (u8 *)kmalloc(ctx->block_size);
    if (!bm) return -1;
    u64 ib = gd.bg_inode_bitmap_lo;
    if (ctx->desc_size >= 64) ib |= (u64)gd.bg_inode_bitmap_hi << 32;
    int rc = e4w_rdblk(ctx, ib, bm);
    if (rc == 0) {
        bm[bit >> 3] &= (u8)~(1u << (bit & 7));
        rc = e4w_wrblk(ctx, ib, bm);
    }
    kfree(bm);
    if (rc != 0) return -1;
    /* zero the table entry */
    fs_ext4_raw_inode_t zero;
    memset(&zero, 0, sizeof(zero));
    e4w_inode_write(ctx, num, &zero);
    e4w_gd_adjust(ctx, group, 0, 1, 0);
    e4w_sb_adjust(ctx, 0, 1);
    return 0;
}

/* ==================================================================
 * VFS ops
 * ================================================================== */

static int e4w_writable(fs_ext4_inode_t *ino) {
    return ino && ino->ctx && (ino->i_flags & E4W_EXTENTS_FLAG);
}

static int e4w_name_ok(const char *name) {
    u32 n = (u32)strlen(name);
    if (n == 0 || n > 255) return 0;
    for (u32 i = 0; i < n; i++)
        if (name[i] == '/') return 0;
    return 1;
}

static int e4w_open(fs_vfs_node_t *node, int flags) {
    if (!node) return -1;
    if (node->type == VFS_TYPE_DIR) return -2;
    fs_ext4_inode_t *ino = (fs_ext4_inode_t *)node->private;
    if (!e4w_writable(ino)) return -2;
    if (flags & VFS_O_TRUNC) {
        /* empty the file: free every block, zero the tree */
        fs_ext4_ctx_t *ctx = ino->ctx;
        if (e4w_ext_free_all(ctx, ino) != 0) return -3;
        fs_ext4_raw_inode_t raw;
        if (e4w_inode_read(ctx, ino->inode_num, &raw) == 0) {
            raw.i_size_lo = 0;
            raw.i_size_hi = 0;
            raw.i_blocks_lo = 0;
            memcpy(raw.i_block, ino->i_block, sizeof(raw.i_block));
            e4w_inode_write(ctx, ino->inode_num, &raw);
        }
        ino->file_size = 0;
        node->size = 0;
    }
    return 0;
}

static int e4w_write(fs_vfs_node_t *node, u64 offset, const void *buf,
                     int size) {
    if (!node || !buf || size <= 0) return -1;
    fs_ext4_inode_t *ino = (fs_ext4_inode_t *)node->private;
    if (!e4w_writable(ino)) return -2;
    fs_ext4_ctx_t *ctx = ino->ctx;
    u32 bsz = ctx->block_size;
    const u8 *src = (const u8 *)buf;
    int total = 0;
    u32 new_blocks = 0;
    u8 *tmp = (u8 *)kmalloc(bsz);
    if (!tmp) return -3;
    u64 end = offset + (u64)size;
    while (total < size) {
        u64 pos = offset + (u64)total;
        u32 logical = (u32)(pos / bsz);
        u32 off = (u32)(pos % bsz);
        u32 chunk = bsz - off;
        if ((u64)chunk > (u64)(size - total)) chunk = (u32)(size - total);
        u64 phys = e4w_ext_lookup(ctx, ino->i_block, logical);
        if (!phys) {
            /* map a whole run of unmapped blocks in one allocation */
            u32 run = (u32)(((u64)(size - total) + off + bsz - 1) / bsz);
            if (run < 1) run = 1;
            if (run > E4W_MAX_RUN) run = E4W_MAX_RUN;
            /* shrink to the actually-unmapped span */
            while (run > 1 &&
                   e4w_ext_lookup(ctx, ino->i_block, logical + run - 1))
                run--;
            u32 alloc = 0;
            u64 goal = 0;
            if (e4w_ext_map(ctx, ino, logical, run, goal, &alloc) != 0)
                break;
            new_blocks += alloc;
            phys = e4w_ext_lookup(ctx, ino->i_block, logical);
            if (!phys) break;
        }
        int rc;
        if (off == 0 && chunk == bsz) {
            rc = e4w_wrblk(ctx, phys, src + total);
        } else {
            rc = e4w_rdblk(ctx, phys, tmp);
            if (rc == 0) {
                memcpy(tmp + off, src + total, chunk);
                rc = e4w_wrblk(ctx, phys, tmp);
            }
        }
        if (rc != 0) break;
        total += (int)chunk;
    }
    kfree(tmp);
    if (total > 0) {
        if (end > ino->file_size) ino->file_size = end;
        e4w_inode_sync(ctx, ino, new_blocks);
        node->size = ino->file_size;
    }
    return total;
}

static int e4w_create(fs_vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_TYPE_DIR || !e4w_name_ok(name))
        return -1;
    fs_ext4_inode_t *pino = (fs_ext4_inode_t *)parent->private;
    if (!e4w_writable(pino)) return -2;
    fs_ext4_ctx_t *ctx = pino->ctx;
    e4w_dirent_pos_t pos;
    if (e4w_dir_scan(ctx, pino, name, &pos) == 0) return -4;   /* exists */
    u32 num = 0;
    if (e4w_ino_alloc(ctx, &num) != 0) return -1;
    fs_ext4_raw_inode_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.i_mode = (u16)(E4W_IFREG | 0644);
    raw.i_links_count = 1;
    raw.i_flags = E4W_EXTENTS_FLAG;
    fs_ext4_extent_header_t *eh = (fs_ext4_extent_header_t *)raw.i_block;
    eh->eh_magic = E4W_EXT_MAGIC;
    eh->eh_entries = 0;
    eh->eh_max = 4;
    eh->eh_depth = 0;
    if (e4w_inode_write(ctx, num, &raw) != 0) {
        e4w_ino_free(ctx, num);
        return -1;
    }
    u32 alloc = 0;
    if (e4w_dir_add(ctx, pino, name, num, E4W_FT_REG, &alloc) != 0) {
        e4w_ino_free(ctx, num);
        return -1;
    }
    parent->size = pino->file_size;
    return e4w_inode_sync(ctx, pino, alloc);
}

static int e4w_mkdir(fs_vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_TYPE_DIR || !e4w_name_ok(name))
        return -1;
    fs_ext4_inode_t *pino = (fs_ext4_inode_t *)parent->private;
    if (!e4w_writable(pino)) return -2;
    fs_ext4_ctx_t *ctx = pino->ctx;
    e4w_dirent_pos_t pos;
    if (e4w_dir_scan(ctx, pino, name, &pos) == 0) return -4;   /* exists */
    u32 num = 0;
    if (e4w_ino_alloc(ctx, &num) != 0) return -1;
    /* one directory block with . .. and a trailing pad entry */
    u64 phys = 0;
    if (e4w_alloc(ctx, 1, 0, &phys) == 0) {
        e4w_ino_free(ctx, num);
        return -1;
    }
    u8 *blk = (u8 *)kmalloc(ctx->block_size);
    if (!blk) { e4w_free(ctx, phys, 1); e4w_ino_free(ctx, num); return -1; }
    memset(blk, 0, ctx->block_size);
    u32 off = 0;
    fs_ext4_dirent_t *dot = (fs_ext4_dirent_t *)(blk + off);
    dot->inode = num; dot->rec_len = 12; dot->name_len = 1;
    dot->file_type = E4W_FT_DIR; dot->name[0] = '.';
    off += 12;
    fs_ext4_dirent_t *dotdot = (fs_ext4_dirent_t *)(blk + off);
    dotdot->inode = pino->inode_num; dotdot->rec_len = 12;
    dotdot->name_len = 2; dotdot->file_type = E4W_FT_DIR;
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    off += 12;
    fs_ext4_dirent_t *pad = (fs_ext4_dirent_t *)(blk + off);
    pad->rec_len = (u16)(ctx->block_size - off);
    int rc = e4w_wrblk(ctx, phys, blk);
    kfree(blk);
    if (rc != 0) { e4w_free(ctx, phys, 1); e4w_ino_free(ctx, num); return -1; }
    fs_ext4_raw_inode_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.i_mode = (u16)(E4W_IFDIR | 0755);
    raw.i_links_count = 2;                    /* . and .. */
    raw.i_size_lo = (u32)ctx->block_size;
    raw.i_blocks_lo = ctx->block_size / 512;
    raw.i_flags = E4W_EXTENTS_FLAG;
    fs_ext4_extent_header_t *eh = (fs_ext4_extent_header_t *)raw.i_block;
    eh->eh_magic = E4W_EXT_MAGIC;
    eh->eh_entries = 1;
    eh->eh_max = 4;
    eh->eh_depth = 0;
    fs_ext4_extent_t *ex = (fs_ext4_extent_t *)((u8 *)eh + 12);
    ex->ee_block = 0;
    ex->ee_len = 1;
    ex->ee_start_hi = (u16)(phys >> 32);
    ex->ee_start_lo = (u32)phys;
    if (e4w_inode_write(ctx, num, &raw) != 0) {
        e4w_free(ctx, phys, 1);
        e4w_ino_free(ctx, num);
        return -1;
    }
    u32 alloc = 0;
    if (e4w_dir_add(ctx, pino, name, num, E4W_FT_DIR, &alloc) != 0) {
        e4w_free(ctx, phys, 1);
        e4w_ino_free(ctx, num);
        return -1;
    }
    /* parent gains a ".." -> one more link; child's group +1 dir */
    fs_ext4_raw_inode_t praw;
    if (e4w_inode_read(ctx, pino->inode_num, &praw) == 0) {
        if (praw.i_links_count < 0xFFFF) praw.i_links_count++;
        e4w_inode_write(ctx, pino->inode_num, &praw);
    }
    u32 cgroup = (num - 1) / ctx->inodes_per_group;
    e4w_gd_adjust(ctx, cgroup, 0, 0, 1);
    parent->size = pino->file_size;
    return e4w_inode_sync(ctx, pino, alloc);
}

static int e4w_rmdir(fs_vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_TYPE_DIR || !e4w_name_ok(name))
        return -1;
    fs_ext4_inode_t *pino = (fs_ext4_inode_t *)parent->private;
    if (!e4w_writable(pino)) return -2;
    fs_ext4_ctx_t *ctx = pino->ctx;
    e4w_dirent_pos_t pos;
    if (e4w_dir_scan(ctx, pino, name, &pos) != 0) return -3;   /* missing */
    fs_ext4_raw_inode_t raw;
    if (e4w_inode_read(ctx, pos.child_ino, &raw) != 0) return -1;
    if ((raw.i_mode & E4W_IFMT) != E4W_IFDIR) return -1;       /* not a dir */
    fs_ext4_inode_t child;
    memset(&child, 0, sizeof(child));
    child.ctx = ctx;
    child.inode_num = pos.child_ino;
    child.file_size = (u64)raw.i_size_lo | ((u64)raw.i_size_hi << 32);
    child.i_flags = raw.i_flags;
    memcpy(child.i_block, raw.i_block, sizeof(child.i_block));
    if (!e4w_writable(&child)) return -2;
    if (!e4w_dir_empty(ctx, &child)) return -5;                /* not empty */
    if (e4w_ext_free_all(ctx, &child) != 0) return -1;
    if (e4w_ino_free(ctx, pos.child_ino) != 0) return -1;
    if (e4w_dir_remove(ctx, pino, name) != 0) return -1;
    fs_ext4_raw_inode_t praw;
    if (e4w_inode_read(ctx, pino->inode_num, &praw) == 0) {
        if (praw.i_links_count > 0) praw.i_links_count--;
        e4w_inode_write(ctx, pino->inode_num, &praw);
    }
    u32 cgroup = (pos.child_ino - 1) / ctx->inodes_per_group;
    e4w_gd_adjust(ctx, cgroup, 0, 0, -1);
    parent->size = pino->file_size;
    return e4w_inode_sync(ctx, pino, 0);
}

static int e4w_unlink(fs_vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_TYPE_DIR || !e4w_name_ok(name))
        return -1;
    fs_ext4_inode_t *pino = (fs_ext4_inode_t *)parent->private;
    if (!e4w_writable(pino)) return -2;
    fs_ext4_ctx_t *ctx = pino->ctx;
    e4w_dirent_pos_t pos;
    if (e4w_dir_scan(ctx, pino, name, &pos) != 0) return -3;   /* missing */
    fs_ext4_raw_inode_t raw;
    if (e4w_inode_read(ctx, pos.child_ino, &raw) != 0) return -1;
    if ((raw.i_mode & E4W_IFMT) == E4W_IFDIR) return -1;       /* use rmdir */
    /* links semantics: files get links_count 1 from create; only free
     * the storage when the last reference disappears */
    if (raw.i_links_count > 1) {
        raw.i_links_count--;
        e4w_inode_write(ctx, pos.child_ino, &raw);
        return e4w_dir_remove(ctx, pino, name);
    }
    fs_ext4_inode_t child;
    memset(&child, 0, sizeof(child));
    child.ctx = ctx;
    child.inode_num = pos.child_ino;
    child.file_size = (u64)raw.i_size_lo | ((u64)raw.i_size_hi << 32);
    child.i_flags = raw.i_flags;
    memcpy(child.i_block, raw.i_block, sizeof(child.i_block));
    if (e4w_writable(&child) && e4w_ext_free_all(ctx, &child) != 0)
        return -1;
    if (e4w_ino_free(ctx, pos.child_ino) != 0) return -1;
    if (e4w_dir_remove(ctx, pino, name) != 0) return -1;
    parent->size = pino->file_size;
    return e4w_inode_sync(ctx, pino, 0);
}

static int e4w_rename(fs_vfs_node_t *parent, const char *oldname,
                      const char *newname) {
    if (!parent || parent->type != VFS_TYPE_DIR) return -1;
    if (!e4w_name_ok(oldname) || !e4w_name_ok(newname)) return -1;
    fs_ext4_inode_t *pino = (fs_ext4_inode_t *)parent->private;
    if (!e4w_writable(pino)) return -2;
    fs_ext4_ctx_t *ctx = pino->ctx;
    e4w_dirent_pos_t pos;
    if (e4w_dir_scan(ctx, pino, oldname, &pos) != 0) return -3;
    fs_ext4_raw_inode_t raw;
    if (e4w_inode_read(ctx, pos.child_ino, &raw) != 0) return -1;
    if ((raw.i_mode & E4W_IFMT) == E4W_IFDIR) return -6;       /* files only */
    e4w_dirent_pos_t exists;
    if (e4w_dir_scan(ctx, pino, newname, &exists) == 0) return -4;
    u32 alloc = 0;
    if (e4w_dir_add(ctx, pino, newname, pos.child_ino, E4W_FT_REG,
                    &alloc) != 0)
        return -1;
    if (e4w_dir_remove(ctx, pino, oldname) != 0) return -1;
    /* keep the cached VFS node coherent (it may already be open) */
    for (fs_vfs_node_t *c = parent->first_child; c; c = c->next_sibling) {
        if (strcmp(c->name, oldname) == 0) {
            strncpy(c->name, newname, VFS_NAME_LEN - 1);
            c->name[VFS_NAME_LEN - 1] = 0;
            break;
        }
    }
    parent->size = pino->file_size;
    return e4w_inode_sync(ctx, pino, alloc);
}

/* ---- attach ---- */

void ocse_ext4w_attach(fs_vfs_file_ops_t *file_ops,
                       fs_vfs_dir_ops_t *dir_ops) {
    file_ops->open = e4w_open;
    file_ops->write = e4w_write;
    dir_ops->create = e4w_create;
    dir_ops->mkdir = e4w_mkdir;
    dir_ops->rmdir = e4w_rmdir;
    dir_ops->unlink = e4w_unlink;
    dir_ops->rename = e4w_rename;
}
