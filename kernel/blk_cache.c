/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/blk_cache.c
 * Purpose: LRU disk-block cache with write-back.
 */
#include "blk_cache.h"
#include "blk.h"
#include "timer.h"
#include "heap.h"
#include "string.h"
#include "console.h"

static blk_cache_slot_t g_cache[BLK_CACHE_SLOTS];
static blk_cache_stats_t g_cache_stats;
static u64 g_cache_clock = 0;   /* monotonic counter for LRU */

void blk_cache_init(void) {
    oc_memset(g_cache, 0, sizeof(g_cache));
    oc_memset(&g_cache_stats, 0, sizeof(g_cache_stats));
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) g_cache[i].dev_idx = -1;
    g_cache_clock = 0;
}

static int find_slot(int dev_idx, u64 lba) {
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx == dev_idx && g_cache[i].lba == lba)
            return i;
    }
    return -1;
}

static int pick_victim(void) {
    /* Find empty slot first. */
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx == -1) return i;
    }
    /* LRU: pick the slot with the smallest last_used. */
    int victim = 0;
    u64 oldest = g_cache[0].last_used;
    for (int i = 1; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].last_used < oldest) {
            oldest = g_cache[i].last_used;
            victim = i;
        }
    }
    /* Write back if dirty. */
    if (g_cache[victim].dirty) {
        blk_write_sectors_raw(g_cache[victim].dev_idx, g_cache[victim].lba, 1, g_cache[victim].data);
        g_cache_stats.writebacks++;
    }
    g_cache_stats.evicts++;
    return victim;
}

int blk_cache_read(int dev_idx, u64 lba, void *buf) {
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        g_cache_stats.misses++;
        return -1;   /* miss — caller reads from disk and calls insert */
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    oc_memcpy(buf, g_cache[s].data, 512);
    g_cache_stats.hits++;
    return 0;
}

int blk_cache_write(int dev_idx, u64 lba, const void *buf) {
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        s = pick_victim();
        g_cache[s].dev_idx = dev_idx;
        g_cache[s].lba = lba;
        
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    oc_memcpy(g_cache[s].data, buf, 512);
    g_cache[s].dirty = 1;
    return 0;
}

int blk_cache_insert(int dev_idx, u64 lba, const void *buf) {
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        s = pick_victim();
        g_cache[s].dev_idx = dev_idx;
        g_cache[s].lba = lba;
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    oc_memcpy(g_cache[s].data, buf, 512);
    g_cache[s].dirty = 0;
    return 0;
}

int blk_cache_flush_dev(int dev_idx) {
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx == dev_idx && g_cache[i].dirty) {
            if (blk_write_sectors_raw(dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0)
                return -1;
            g_cache[i].dirty = 0;
            g_cache_stats.writebacks++;
        }
    }
    return 0;
}

int blk_cache_flush(void) {
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx != -1 && g_cache[i].dirty) {
            if (blk_write_sectors_raw(g_cache[i].dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0)
                return -1;
            g_cache[i].dirty = 0;
            g_cache_stats.writebacks++;
        }
    }
    return 0;
}

void blk_cache_get_stats(blk_cache_stats_t *out) {
    if (out) {
        *out = g_cache_stats;
        /* BUG-015 FIX: Compute used count by scanning slots.
         * Old code never set this field, so lsblk always showed 0/64. */
        out->used = 0;
        for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
            if (g_cache[i].dev_idx != -1) out->used++;
        }
    }
}
