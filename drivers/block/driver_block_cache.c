/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/driver_block_cache.c
 * Purpose: LRU disk-block cache with write-back.
 */
#include "driver_block_cache.h"
#include "driver_block_blk.h"
#include "core_timer.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"

static driver_block_cache_slot_t g_cache[BLK_CACHE_SLOTS];
static driver_block_cache_stats_t g_cache_stats;
static u64 g_cache_clock = 0;   /* monotonic counter for LRU */

void driver_block_cache_init(void) {
    memset(g_cache, 0, sizeof(g_cache));
    memset(&g_cache_stats, 0, sizeof(g_cache_stats));
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

/* BUG-0116 FIX: pick a victim slot. The old code picked the LRU slot,
 * fired one write-back for a dirty page and IGNORED the return value -
 * a device error silently dropped the data (the slot got reused). Now
 * victims are tried in LRU order: a clean slot is taken immediately, a
 * dirty slot is only released when its write-back SUCCEEDS. If every
 * dirty slot fails to write back the function returns -1 and the
 * callers fall back to write-through instead of losing data. */
static int pick_victim(void) {
    /* Find empty slot first. */
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx == -1) return i;
    }
    u8 tried[BLK_CACHE_SLOTS];
    memset(tried, 0, sizeof(tried));
    for (;;) {
        int vi = -1;
        u64 oldest = ~0ull;
        for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
            if (tried[i]) continue;
            if (g_cache[i].last_used < oldest) {
                oldest = g_cache[i].last_used;
                vi = i;
            }
        }
        if (vi < 0) return -1;   /* every candidate failed its write-back */
        if (!g_cache[vi].dirty) return vi;
        if (driver_block_write_sectors_raw(g_cache[vi].dev_idx, g_cache[vi].lba, 1,
                                           g_cache[vi].data) == 0) {
            g_cache[vi].dirty = 0;
            g_cache_stats.writebacks++;
            return vi;
        }
        /* write-back failed: KEEP the dirty slot and try the next one */
        tried[vi] = 1;
    }
}

int driver_block_cache_read(int dev_idx, u64 lba, void *buf) {
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        g_cache_stats.misses++;
        return -1;   /* miss — caller reads from disk and calls insert */
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    memcpy(buf, g_cache[s].data, 512);
    g_cache_stats.hits++;
    return 0;
}

int driver_block_cache_write(int dev_idx, u64 lba, const void *buf) {
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        s = pick_victim();
        /* BUG-0116 FIX: no reclaimable slot (write-backs failing). Do
         * NOT index g_cache[-1]: fall back to write-through so the data
         * reaches the device and the error propagates to the caller. */
        if (s < 0) {
            return driver_block_write_sectors_raw(dev_idx, lba, 1, buf);
        }
        g_cache[s].dev_idx = dev_idx;
        g_cache[s].lba = lba;
        g_cache_stats.evicts++;
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    memcpy(g_cache[s].data, buf, 512);
    g_cache[s].dirty = 1;
    return 0;
}

int driver_block_cache_insert(int dev_idx, u64 lba, const void *buf) {
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        s = pick_victim();
        /* BUG-0116 FIX: honour pick_victim failure instead of writing
         * g_cache[-1]. The caller already has the data, so a failed
         * insert only means "not cached". */
        if (s < 0) return -1;
        g_cache[s].dev_idx = dev_idx;
        g_cache[s].lba = lba;
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    memcpy(g_cache[s].data, buf, 512);
    g_cache[s].dirty = 0;
    return 0;
}

int driver_block_cache_flush_dev(int dev_idx) {
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx == dev_idx && g_cache[i].dirty) {
            if (driver_block_write_sectors_raw(dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0)
                return -1;
            g_cache[i].dirty = 0;
            g_cache_stats.writebacks++;
        }
    }
    return 0;
}

int driver_block_cache_flush(void) {
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx != -1 && g_cache[i].dirty) {
            if (driver_block_write_sectors_raw(g_cache[i].dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0)
                return -1;
            g_cache[i].dirty = 0;
            g_cache_stats.writebacks++;
        }
    }
    return 0;
}

void driver_block_cache_get_stats(driver_block_cache_stats_t *out) {
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
