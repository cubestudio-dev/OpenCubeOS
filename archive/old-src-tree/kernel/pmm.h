/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-03
 * File: kernel/pmm.h
 * Purpose: Physical Memory Manager - bitmap allocator for 4KB page frames.
 *
 * Parses the multiboot2 memory map, builds a bitmap of all physical pages,
 * and provides O(1) alloc/free. Reserved regions (kernel code, framebuffer,
 * multiboot2 info) are marked as used at init time.
 */
#ifndef OC_PMM_H
#define OC_PMM_H

#include "types.h"
#include "multiboot2.h"

#define PMM_PAGE_SIZE 4096
#define PMM_PAGE_SHIFT 12

typedef struct pmm_stats {
    u64 total_pages;    /* total usable pages in the system */
    u64 used_pages;     /* pages currently allocated or reserved */
    u64 free_pages;     /* pages available for allocation */
    u64 total_bytes;    /* total_pages * 4096 */
    u64 used_bytes;     /* used_pages * 4096 */
    u64 free_bytes;     /* free_pages * 4096 */
    u64 free_fragments; /* number of contiguous free runs (lower = less frag) */
} pmm_stats_t;

/* Initialize PMM from multiboot2 info. Must be called before any alloc. */
void pmm_init(const oc_mb2_info_t *mbi);

/* Allocate a single 4KB page frame. Returns physical address, 0 on failure.
 * If no pages are available, calls emergency callbacks before failing. */
u64 pmm_alloc_frame(void);

/* Allocate `count` physically-contiguous 4KB pages. Returns physical address
 * of the first page, 0 on failure. WP-07: needed by the heap for large
 * allocations (shell token arrays ~40KB). */
u64 pmm_alloc_contig(u64 count);

/* Free a previously allocated page frame. */
void pmm_free_frame(u64 paddr);

/* Reserve a physical region [start, start+size) so it won't be allocated. */
void pmm_reserve_region(u64 start, u64 size);

/* Query statistics. */
void pmm_get_stats(pmm_stats_t *out);

/* Emergency callback: called when PMM is about to return 0 (out of memory).
 * L1 can register a callback to free cached pages before the allocation fails. */
typedef void (*pmm_emergency_cb_fn)(void);
void pmm_register_emergency_callback(pmm_emergency_cb_fn fn);

#endif /* OC_PMM_H */
