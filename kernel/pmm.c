/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03
 * File: kernel/pmm.c
 * Purpose: Physical Memory Manager implementation.
 *
 * Bitmap allocator: 1 bit per 4KB page. The bitmap covers all of physical
 * RAM up to 4 GiB (the identity-mapped region). Pages are allocated by
 * scanning the bitmap for the first clear bit.
 */
#include "pmm.h"
#include "string.h"
#include "multiboot2.h"

/* Bitmap: 1 bit per page. Max 4 GiB / 4 KiB = 1M pages = 128 KiB bitmap. */
#define PMM_MAX_PAGES (1024 * 1024)  /* 4 GiB / 4 KiB */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)  /* 128 KiB */

static u8  g_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));
static u64 g_total_pages = 0;
static u64 g_used_pages  = 0;
static u64 g_free_pages  = 0;
static u64 g_last_scan   = 0;  /* hint: start scanning from here */

/* Emergency callbacks (up to 4). */
#define PMM_MAX_EMERGENCY_CBS 4
static pmm_emergency_cb_fn g_emergency_cbs[PMM_MAX_EMERGENCY_CBS];

static inline void bitmap_set(u64 page_idx) {
    g_bitmap[page_idx >> 3] |= (1 << (page_idx & 7));
}

static inline void bitmap_clear(u64 page_idx) {
    g_bitmap[page_idx >> 3] &= ~(1 << (page_idx & 7));
}

static inline int bitmap_test(u64 page_idx) {
    return (g_bitmap[page_idx >> 3] >> (page_idx & 7)) & 1;
}

void pmm_reserve_region(u64 start, u64 size) {
    u64 first_page = start >> PMM_PAGE_SHIFT;
    u64 last_page  = (start + size - 1) >> PMM_PAGE_SHIFT;
    for (u64 i = first_page; i <= last_page && i < g_total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            g_used_pages++;
            g_free_pages--;
        }
    }
}

void pmm_init(const oc_mb2_info_t *mbi) {
    /* Clear the bitmap: initially everything is reserved (1 = used). */
    oc_memset(g_bitmap, 0xFF, sizeof(g_bitmap));
    g_total_pages = 0;
    g_used_pages  = 0;
    g_free_pages  = 0;
    g_last_scan   = 0;

    if (!mbi || !mbi->mmap) return;

    /* Walk the mmap entries. For each AVAILABLE region, mark pages as free. */
    const oc_mb2_mmap_tag_t *mmap = mbi->mmap;
    u32 entry_size = mmap->entry_size;
    u32 entries = (mmap->size - sizeof(*mmap)) / entry_size;

    u64 max_page = 0;

    for (u32 i = 0; i < entries; i++) {
        const oc_mb2_mmap_entry_t *e =
            (const oc_mb2_mmap_entry_t*)((const u8*)mmap + sizeof(*mmap) + i * entry_size);
        if (e->type == 1) {  /* Available RAM */
            u64 start_page = e->addr >> PMM_PAGE_SHIFT;
            u64 end_page   = (e->addr + e->len) >> PMM_PAGE_SHIFT;
            if (end_page > PMM_MAX_PAGES) end_page = PMM_MAX_PAGES;
            for (u64 p = start_page; p < end_page; p++) {
                bitmap_clear(p);
                g_free_pages++;
            }
            if (end_page > max_page) max_page = end_page;
        }
    }

    g_total_pages = max_page;

    /* Reserve the first 1 MiB (BIOS area, low memory). */
    pmm_reserve_region(0, 0x100000);

    /* Reserve the kernel image. The kernel is loaded at 1 MiB. We reserve
     * from 1 MiB to the end of BSS. We use the linker symbols to find the
     * end. */
    extern u8 __end_of_kernel[];
    u64 kern_end = (u64)(uintptr_t)__end_of_kernel;
    /* Round up to page boundary. */
    kern_end = (kern_end + 0xFFF) & ~0xFFF;
    pmm_reserve_region(0x100000, kern_end - 0x100000);

    /* Reserve the framebuffer. */
    if (mbi->fb) {
        u64 fb_addr = mbi->fb->framebuffer_addr;
        u64 fb_size = (u64)mbi->fb->framebuffer_pitch * mbi->fb->framebuffer_height;
        fb_size = (fb_size + 0xFFF) & ~0xFFF;
        pmm_reserve_region(fb_addr, fb_size);
    }

    /* Reserve the multiboot2 info structure. */
    if (mbi->header) {
        u64 mbi_size = mbi->header->total_size;
        mbi_size = (mbi_size + 0xFFF) & ~0xFFF;
        /* The mbi is at a physical address; we reserve from there. */
        pmm_reserve_region((u64)(uintptr_t)mbi->header, mbi_size);
    }

    /* Reserve the bitmap itself. */
    pmm_reserve_region((u64)(uintptr_t)g_bitmap, sizeof(g_bitmap));
}

u64 pmm_alloc_frame(void) {
    /* P1-1 FIX: guard against g_total_pages == 0 (div by zero). */
    if (g_total_pages == 0) return 0;
    /* Scan from g_last_scan for a free page. */
    for (u64 i = 0; i < g_total_pages; i++) {
        u64 idx = (g_last_scan + i) % g_total_pages;
        if (!bitmap_test(idx)) {
            bitmap_set(idx);
            g_used_pages++;
            g_free_pages--;
            g_last_scan = idx + 1;
            return idx << PMM_PAGE_SHIFT;
        }
    }

    /* Out of memory. Call emergency callbacks. */
    for (int i = 0; i < PMM_MAX_EMERGENCY_CBS; i++) {
        if (g_emergency_cbs[i]) {
            g_emergency_cbs[i]();
            /* Retry after emergency free. */
            for (u64 j = 0; j < g_total_pages; j++) {
                if (!bitmap_test(j)) {
                    bitmap_set(j);
                    g_used_pages++;
                    g_free_pages--;
                    g_last_scan = j + 1;
                    return j << PMM_PAGE_SHIFT;
                }
            }
        }
    }

    return 0;  /* truly out of memory */
}

/* WP-07: Allocate `count` physically-contiguous pages. Scans the bitmap for
 * a run of `count` free pages. Returns physical address or 0 on failure. */
u64 pmm_alloc_contig(u64 count) {
    if (count == 0) return 0;
    for (u64 i = 0; i < g_total_pages; i++) {
        /* Check if pages i..i+count-1 are all free. */
        int ok = 1;
        for (u64 j = 0; j < count; j++) {
            if (i + j >= g_total_pages || bitmap_test(i + j)) { ok = 0; break; }
        }
        if (ok) {
            for (u64 j = 0; j < count; j++) {
                bitmap_set(i + j);
            }
            g_used_pages += count;
            g_free_pages -= count;
            g_last_scan = i + count;
            return i << PMM_PAGE_SHIFT;
        }
    }
    return 0;
}

void pmm_free_frame(u64 paddr) {
    if (paddr == 0) return;
    u64 idx = paddr >> PMM_PAGE_SHIFT;
    if (idx >= g_total_pages) return;
    if (bitmap_test(idx)) {
        bitmap_clear(idx);
        g_used_pages--;
        g_free_pages++;
        if (idx < g_last_scan) g_last_scan = idx;
    }
}

void pmm_get_stats(pmm_stats_t *out) {
    if (!out) return;
    /* WP-04 fix: compute used = total - free to ensure consistency.
     * The g_used_pages counter can drift from the actual bitmap state
     * because pages that were never marked available (memory holes)
     * are neither counted as used nor free. */
    out->total_pages = g_total_pages;
    out->free_pages  = g_free_pages;
    out->used_pages  = g_total_pages - g_free_pages;
    out->total_bytes = g_total_pages << PMM_PAGE_SHIFT;
    out->used_bytes  = out->used_pages << PMM_PAGE_SHIFT;
    out->free_bytes  = g_free_pages << PMM_PAGE_SHIFT;

    /* Count free fragments (contiguous free runs). */
    out->free_fragments = 0;
    int in_run = 0;
    for (u64 i = 0; i < g_total_pages; i++) {
        if (!bitmap_test(i)) {
            if (!in_run) {
                out->free_fragments++;
                in_run = 1;
            }
        } else {
            in_run = 0;
        }
    }
}

void pmm_register_emergency_callback(pmm_emergency_cb_fn fn) {
    if (!fn) return;
    for (int i = 0; i < PMM_MAX_EMERGENCY_CBS; i++) {
        if (g_emergency_cbs[i] == NULL) {
            g_emergency_cbs[i] = fn;
            return;
        }
    }
}
