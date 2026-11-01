/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: drivers/block/driver_block_cache.c
 * Purpose: LRU disk-block cache with write-back.
 */
#include "driver_block_cache.h"
#include "driver_block_blk.h"
#include "core_timer.h"
#include "core_sync.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"

/* WP-11-crash_recovery layout change (behaviour-equivalent): the slot
 * table moved from .bss to a boot-time kmalloc block. The 0x400000
 * identity-window had ~7 KiB of headroom left and the WP-11 crash
 * machinery (panic path + dump + log) needed more than that, so the
 * single largest .bss object that (a) is initialised AFTER the heap
 * and (b) already memsets itself in driver_block_cache_init() was
 * relocated. Every public entry keeps the EXACT semantics: NULL table
 * = permanent miss = the callers' existing raw-I/O fallback; init
 * still zeroes every slot and marks dev_idx = -1. */
static driver_block_cache_slot_t *g_cache = NULL;
static driver_block_cache_stats_t g_cache_stats;
static u64 g_cache_clock = 0;   /* monotonic counter for LRU */
/* BUG-0314 (A6-18): the whole cache used to be lock-free - find_slot /
 * pick_victim / g_cache_clock / eviction write-backs raced between tasks
 * (double write-back, lost dirty pages). The block cache can be reached
 * from several task contexts (shell I/O, OTA installer, L1), so every
 * public operation now runs under one irqsave spinlock (BUG-0285
 * semantics). Raw device I/O inside pick_victim/flush uses the RAW path,
 * which never re-enters this lock, so the critical sections are flat. */
static spinlock_t g_cache_lock;

void driver_block_cache_init(void) {
    spin_init(&g_cache_lock);
    spin_lock(&g_cache_lock);
    if (!g_cache) {
        g_cache = kmalloc(sizeof(driver_block_cache_slot_t) * BLK_CACHE_SLOTS);
        if (!g_cache) {
            /* boot-time 34 KiB alloc failed: cache stays disabled.
             * Reads/writes fall back to raw device I/O (the same path
             * a permanent miss would take) - reported, not silent. */
            spin_unlock(&g_cache_lock);
            screen_console_puts("blkcache: alloc FAILED - cache disabled (raw I/O)\n");
            return;
        }
    }
    memset(g_cache, 0, sizeof(driver_block_cache_slot_t) * BLK_CACHE_SLOTS);
    memset(&g_cache_stats, 0, sizeof(g_cache_stats));
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) g_cache[i].dev_idx = -1;
    g_cache_clock = 0;
    spin_unlock(&g_cache_lock);
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
    if (!g_cache) return -1;   /* disabled: permanent miss */
    spin_lock(&g_cache_lock);
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        g_cache_stats.misses++;
        spin_unlock(&g_cache_lock);
        return -1;   /* miss — caller reads from disk and calls insert */
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    memcpy(buf, g_cache[s].data, 512);
    g_cache_stats.hits++;
    spin_unlock(&g_cache_lock);
    return 0;
}

int driver_block_cache_write(int dev_idx, u64 lba, const void *buf) {
    if (!g_cache) {
        /* disabled: write-through to the device, same as a failed
         * victim reclaim below */
        return driver_block_write_sectors_raw(dev_idx, lba, 1, buf);
    }
    spin_lock(&g_cache_lock);
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        s = pick_victim();
        /* BUG-0116 FIX: no reclaimable slot (write-backs failing). Do
         * NOT index g_cache[-1]: fall back to write-through so the data
         * reaches the device and the error propagates to the caller. */
        if (s < 0) {
            int rc = driver_block_write_sectors_raw(dev_idx, lba, 1, buf);
            spin_unlock(&g_cache_lock);
            return rc;
        }
        g_cache[s].dev_idx = dev_idx;
        g_cache[s].lba = lba;
        g_cache_stats.evicts++;
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    memcpy(g_cache[s].data, buf, 512);
    g_cache[s].dirty = 1;
    spin_unlock(&g_cache_lock);
    return 0;
}

int driver_block_cache_insert(int dev_idx, u64 lba, const void *buf) {
    if (!g_cache) return -1;   /* disabled: not cached */
    spin_lock(&g_cache_lock);
    int s = find_slot(dev_idx, lba);
    if (s < 0) {
        s = pick_victim();
        /* BUG-0116 FIX: honour pick_victim failure instead of writing
         * g_cache[-1]. The caller already has the data, so a failed
         * insert only means "not cached". */
        if (s < 0) {
            spin_unlock(&g_cache_lock);
            return -1;
        }
        g_cache[s].dev_idx = dev_idx;
        g_cache[s].lba = lba;
    }
    g_cache_clock++;
    g_cache[s].last_used = g_cache_clock;
    memcpy(g_cache[s].data, buf, 512);
    g_cache[s].dirty = 0;
    spin_unlock(&g_cache_lock);
    return 0;
}

int driver_block_cache_flush_dev(int dev_idx) {
    if (!g_cache) return 0;    /* disabled: nothing dirty */
    spin_lock(&g_cache_lock);
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx == dev_idx && g_cache[i].dirty) {
            if (driver_block_write_sectors_raw(dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0) {
                spin_unlock(&g_cache_lock);
                return -1;
            }
            g_cache[i].dirty = 0;
            g_cache_stats.writebacks++;
        }
    }
    spin_unlock(&g_cache_lock);
    return 0;
}

int driver_block_cache_flush(void) {
    if (!g_cache) return 0;    /* disabled: nothing dirty */
    spin_lock(&g_cache_lock);
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_cache[i].dev_idx != -1 && g_cache[i].dirty) {
            if (driver_block_write_sectors_raw(g_cache[i].dev_idx, g_cache[i].lba, 1, g_cache[i].data) != 0) {
                spin_unlock(&g_cache_lock);
                return -1;
            }
            g_cache[i].dirty = 0;
            g_cache_stats.writebacks++;
        }
    }
    spin_unlock(&g_cache_lock);
    return 0;
}

void driver_block_cache_get_stats(driver_block_cache_stats_t *out) {
    if (out) {
        spin_lock(&g_cache_lock);
        *out = g_cache_stats;
        /* BUG-015 FIX: Compute used count by scanning slots.
         * Old code never set this field, so lsblk always showed 0/64. */
        out->used = 0;
        for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
            if (g_cache[i].dev_idx != -1) out->used++;
        }
        spin_unlock(&g_cache_lock);
    }
}
