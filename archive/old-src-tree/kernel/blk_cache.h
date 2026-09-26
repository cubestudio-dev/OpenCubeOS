/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/blk_cache.h
 * Purpose: LRU disk-block cache with write-back policy.
 *
 * L1 extension interface (item 31): blk_cache_stats
 * L1 extension interface (item 32): blk_cache_flush
 */
#ifndef OC_BLK_CACHE_H
#define OC_BLK_CACHE_H

#include "types.h"

#define BLK_CACHE_SLOTS  64   /* 64 * 512 = 32 KiB cache */

typedef struct {
    int  dev_idx;        /* -1 = empty slot */
    u64  lba;
    u8   data[512];
    u8   dirty;          /* 1 = needs write-back */
    u64  last_used;      /* tick count for LRU */
} blk_cache_slot_t;

typedef struct {
    u64 hits;
    u64 misses;
    u64 evicts;
    u64 writebacks;
    u32 used;            /* slots in use */
} blk_cache_stats_t;

void blk_cache_init(void);

/* Read a sector from cache. Returns 0 on hit (data filled), -1 on miss. */
int blk_cache_read(int dev_idx, u64 lba, void *buf);

/* Write a sector into cache (mark dirty). */
int blk_cache_write(int dev_idx, u64 lba, const void *buf);

/* Insert a freshly-read sector into cache (not dirty). */
int blk_cache_insert(int dev_idx, u64 lba, const void *buf);

/* Flush all dirty blocks to disk. Returns 0 on success. */
int blk_cache_flush(void);

/* Flush dirty blocks for a specific device. */
int blk_cache_flush_dev(int dev_idx);

/* Get cache statistics. */
void blk_cache_get_stats(blk_cache_stats_t *out);

#endif /* OC_BLK_CACHE_H */
