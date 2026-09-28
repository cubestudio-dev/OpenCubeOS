/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03
 * File: kernel/heap.c
 * Purpose: Kernel heap implementation.
 *
 * Free-list allocator with first-fit and block coalescing. The heap pool
 * is backed by physical pages from PMM (identity-mapped in kernel space).
 *
 * Block layout:
 *   [header][payload ...]
 * Header: 32 bytes (magic, size, free flag, prev pointer, next pointer)
 * Payload is 16-byte aligned.
 */
#include "heap.h"
#include "pmm.h"
#include "string.h"

#define HEAP_MAGIC      0xDEADBEEFCAFEBABEULL
#define HEAP_MAGIC_FREE 0xFEEDFACE12345678ULL
#define HEAP_MIN_POOL_PAGES 16   /* 64 KiB initial pool (shell needs ~40KB for tokens) */
#define HEAP_MAX_POOL_PAGES 256  /* BUG-041 FIX: was 64 (256 KiB), now 256 (1 MiB).
                                  * Old limit caused >256KB allocations to fail. */
#define HEAP_ALIGN 16

typedef struct heap_block {
    u64 magic;           /* HEAP_MAGIC if allocated, HEAP_MAGIC_FREE if free */
    u64 size;            /* payload size in bytes (excludes header) */
    struct heap_block *next;  /* next block in the free list (free blocks only) */
    struct heap_block *prev;  /* prev block in the free list */
    u64 _pad;            /* align payload to 16 bytes (header is 32 bytes) */
} heap_block_t;

/* P2-22 NOTE: The heap is accessed from both kernel code and IRQ
 * handlers. On this single-CPU system, IRQs can preempt kernel code
 * but not other IRQs (8259 PIC). kmalloc/kfree are called from:
 *   - kernel shell (no IRQ nesting, safe)
 *   - timer IRQ (soft timer callbacks — these call kmalloc)
 *   - page fault handler (calls kmalloc for PT allocation)
 * Since the timer IRQ is the only concurrent caller, and it calls
 * kmalloc only from soft_timer_test_cb (which runs rarely), the
 * race window is extremely narrow. For production use, a spinlock
 * should be added. For now, we document this as a known limitation.
 * The free list is a singly-linked list of free blocks. */
static heap_block_t *g_free_list = NULL;
static u64 g_heap_size = 0;
static u64 g_allocated = 0;
static u64 g_overhead = 0;
static u64 g_alloc_count = 0;
static u64 g_free_count = 0;
static u64 g_total_allocs = 0;
static u64 g_total_frees = 0;
static heap_hook_fn g_hook = NULL;

/* Pool regions allocated from PMM. We track them so we could (in theory)
 * return them to PMM. Max 16 pool regions.
 * P7 fix: also track pool base addresses so kfree can check if two
 * adjacent blocks are in the same contiguous pool (for cross-page
 * coalescing). */
static u64 g_pool_pages[16];
static u64 g_pool_bases[16];  /* P7: base VA of each pool (0 = single-page fallback) */
static int g_pool_count = 0;

/* P7: check if two addresses are in the same contiguous pool. */
static int in_same_pool(u64 a1, u64 a2) {
    for (int i = 0; i < g_pool_count; i++) {
        u64 base = g_pool_bases[i];
        if (base == 0) continue;  /* single-page pool — not contiguous */
        u64 end = base + g_pool_pages[i] * PMM_PAGE_SIZE;
        if (a1 >= base && a1 < end && a2 >= base && a2 < end) return 1;
    }
    return 0;
}

static void add_pool(u64 page_count) {
    if (g_pool_count >= 16) return;
    /* WP-07: allocate a contiguous run of pages so the heap can satisfy
     * large allocations (shell token arrays need ~40KB contiguous).
     * Previously each page was a separate 4KB block, which couldn't
     * satisfy any allocation > 4KB. */
    u64 phys = pmm_alloc_contig(page_count);
    if (phys == 0) {
        /* Fallback: allocate single pages (old behavior). */
        for (u64 i = 0; i < page_count; i++) {
            u64 p = pmm_alloc_frame();
            if (p == 0) break;
            heap_block_t *blk = (heap_block_t*)p;
            blk->magic = HEAP_MAGIC_FREE;
            blk->size = PMM_PAGE_SIZE - sizeof(heap_block_t);
            blk->prev = NULL;
            blk->next = g_free_list;
            blk->_pad = 0;
            if (g_free_list) g_free_list->prev = blk;
            g_free_list = blk;
            g_heap_size += PMM_PAGE_SIZE;
            g_overhead += sizeof(heap_block_t);
            g_free_count++;
        }
        g_pool_pages[g_pool_count] = page_count;
        g_pool_bases[g_pool_count] = 0; /* single-page fallback */
        g_pool_count++;
        return;
    }
    /* One big contiguous free block spanning all pages. */
    heap_block_t *blk = (heap_block_t*)phys;
    blk->magic = HEAP_MAGIC_FREE;
    blk->size = page_count * PMM_PAGE_SIZE - sizeof(heap_block_t);
    blk->prev = NULL;
    blk->next = g_free_list;
    blk->_pad = 0;
    if (g_free_list) g_free_list->prev = blk;
    g_free_list = blk;
    g_heap_size += page_count * PMM_PAGE_SIZE;
    g_overhead += sizeof(heap_block_t);
    g_free_count++;
    g_pool_pages[g_pool_count] = page_count;
    g_pool_bases[g_pool_count] = phys; /* P7: record contiguous pool base */
    g_pool_count++;
}

void heap_init(void) {
    g_free_list = NULL;
    g_heap_size = 0;
    g_allocated = 0;
    g_overhead = 0;
    g_alloc_count = 0;
    g_free_count = 0;
    g_total_allocs = 0;
    g_total_frees = 0;
    g_hook = NULL;
    g_pool_count = 0;

    add_pool(HEAP_MIN_POOL_PAGES);
}

/* Find a free block that fits. First-fit. */
static heap_block_t *find_free(u64 size) {
    for (heap_block_t *b = g_free_list; b; b = b->next) {
        if (b->magic == HEAP_MAGIC_FREE && b->size >= size) {
            return b;
        }
    }
    return NULL;
}

/* Remove a block from the free list. */
static void remove_free(heap_block_t *b) {
    if (b->prev) b->prev->next = b->next;
    else         g_free_list = b->next;
    if (b->next) b->next->prev = b->prev;
    b->next = NULL;
    b->prev = NULL;
    g_free_count--;
}

/* Add a block to the free list. */
static void add_free(heap_block_t *b) {
    b->next = g_free_list;
    b->prev = NULL;
    if (g_free_list) g_free_list->prev = b;
    g_free_list = b;
    g_free_count++;
}

/* Try to split a block if it's much larger than needed. */
static void try_split(heap_block_t *b, u64 needed) {
    /* Only split if the remainder can hold a header + at least 16 bytes. */
    if (b->size < needed + sizeof(heap_block_t) + HEAP_ALIGN) return;

    heap_block_t *new_blk = (heap_block_t*)((u8*)b + sizeof(heap_block_t) + needed);
    new_blk->magic = HEAP_MAGIC_FREE;
    new_blk->size = b->size - needed - sizeof(heap_block_t);
    new_blk->next = NULL;
    new_blk->prev = NULL;
    new_blk->_pad = 0;

    b->size = needed;
    add_free(new_blk);
    g_overhead += sizeof(heap_block_t);
}

/* P2-22 FIX: Real spinlock for the heap. Uses cli/sti to prevent IRQ
 * preemption (timer IRQ calls kmalloc from soft_timer callbacks).
 * This prevents free-list corruption when kmalloc is called from
 * both kernel code and an IRQ handler concurrently. */
static volatile u64 g_heap_lock = 0;

static inline void heap_lock_acquire(u64 *flags) {
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(*flags));
    while (__atomic_test_and_set(&g_heap_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static inline void heap_lock_release(u64 flags) {
    __atomic_clear(&g_heap_lock, __ATOMIC_RELEASE);
    __asm__ volatile("pushq %0; popfq" : : "r"(flags));
}

void *kmalloc(u64 size) {
    if (size == 0) return NULL;

    /* Round up to alignment. */
    size = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);

    u64 irq_flags;
    heap_lock_acquire(&irq_flags);

    heap_block_t *b = find_free(size);
    if (!b) {
        /* Expand the heap. */
        u64 pages_needed = (size + sizeof(heap_block_t) + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
        if (pages_needed < 4) pages_needed = 4;
        if (pages_needed > HEAP_MAX_POOL_PAGES) pages_needed = HEAP_MAX_POOL_PAGES;
        add_pool(pages_needed);
        b = find_free(size);
        if (!b) { heap_lock_release(irq_flags); return NULL; }
    }

    /* Try to split if the block is much larger. */
    try_split(b, size);

    /* Mark as allocated. */
    remove_free(b);
    b->magic = HEAP_MAGIC;
    b->size = size;

    g_allocated += size;
    g_alloc_count++;
    g_total_allocs++;

    if (g_hook) g_hook((u8*)b + sizeof(heap_block_t), size, 1);

    heap_lock_release(irq_flags);
    return (u8*)b + sizeof(heap_block_t);
}

void *kzalloc(u64 size) {
    void *p = kmalloc(size);
    if (p) oc_memset(p, 0, size);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;

    u64 irq_flags;
    heap_lock_acquire(&irq_flags);

    heap_block_t *b = (heap_block_t*)((u8*)ptr - sizeof(heap_block_t));

    /* Double-free detection. */
    if (b->magic == HEAP_MAGIC_FREE) {
        /* Double free! Don't crash, just ignore. */
        return;
    }
    if (b->magic != HEAP_MAGIC) {
        /* Not a valid heap block. Ignore. */
        return;
    }

    b->magic = HEAP_MAGIC_FREE;
    g_allocated -= b->size;
    g_alloc_count--;
    g_total_frees++;

    if (g_hook) g_hook(ptr, b->size, 0);

    add_free(b);

    /* Coalesce with adjacent free blocks. Since our pool is made of
     * non-contiguous 4K pages, we can only coalesce blocks within the
     * same page. Check if the block before us (within the same page) is free.
     *
     * P4 fix: the old code required both blocks to be in the SAME 4 KiB
     * page. This prevented coalescing across page boundaries even when
     * the pool was a contiguous multi-page run (WP-07's pmm_alloc_contig).
     * The fix is to check if both blocks are in the same CONTIGUOUS POOL
     * rather than the same PAGE. Since we don't track pool bases, we use
     * the heuristic: if two blocks are exactly adjacent in VA, they're
     * in the same pool (the heap never allocates blocks that span pool
     * boundaries). This is safe because pmm_alloc_contig returns
     * physically-contiguous pages that are also VA-contiguous.
     *
     * However, to avoid any risk of cross-pool corruption (two separate
     * single-page pools that happen to be VA-adjacent), we keep the
     * same-page check for single-page pools and only allow cross-page
     * coalescing when both blocks are in the same 16-page-or-larger pool.
     * Since we don't track pool membership, we conservatively require
     * same-page for now. A future fix could add pool-base tracking. */
    u64 block_start = (u64)(uintptr_t)b;
    u64 block_end = block_start + sizeof(heap_block_t) + b->size;
    u64 page_start = block_start & ~(PMM_PAGE_SIZE - 1);

    /* P7 fix: Backward coalescing. The old code had a critical bug:
     * the return after backward coalesce did NOT call heap_lock_release,
     * leaving the heap spinlock locked. The next kmalloc/kfree would
     * spin forever on heap_lock_acquire → heaptest hung after the 2nd
     * kfree (which triggered the first backward coalesce). */

    /* Walk the free list looking for a block that ends right where we start. */
    for (heap_block_t *f = g_free_list; f; f = f->next) {
        if (f->magic != HEAP_MAGIC_FREE) continue;
        u64 f_end = (u64)(uintptr_t)f + sizeof(heap_block_t) + f->size;
        if (f_end == block_start) {
            /* P7: check same-page (for single-page pools) OR same
             * contiguous pool (for multi-page pools, tracked by
             * g_pool_bases). */
            int same_page = ((u64)(uintptr_t)f & ~(PMM_PAGE_SIZE - 1)) == page_start
                         || (u64)(uintptr_t)f >= page_start;
            /* Actually, for backward coalesce: f is BEFORE b, so f could
             * be on the same page or the previous page. The old check
             * (f >= page_start) only allowed same-page. For contiguous
             * pools, we need to also allow the previous page if both
             * blocks are in the same pool. */
            int same_pool = in_same_pool((u64)(uintptr_t)f, block_start);
            if (same_page || same_pool) {
                /* Coalesce: merge b into f. */
                remove_free(b);
                f->size += sizeof(heap_block_t) + b->size;
                g_overhead -= sizeof(heap_block_t);
                heap_lock_release(irq_flags);  /* P7 FIX: was missing! */
                return;
            }
        }
    }

    /* Forward coalescing — check if the block AFTER us is free. */
    for (heap_block_t *f = g_free_list; f; f = f->next) {
        if (f->magic != HEAP_MAGIC_FREE) continue;
        u64 f_start = (u64)(uintptr_t)f;
        if (f_start == block_end) {
            int same_page = (f_start & ~(PMM_PAGE_SIZE - 1)) == page_start;
            int same_pool = in_same_pool(f_start, block_start);
            if (same_page || same_pool) {
                /* Coalesce: merge f into b. */
                remove_free(f);
                b->size += sizeof(heap_block_t) + f->size;
                g_overhead -= sizeof(heap_block_t);
                heap_lock_release(irq_flags);
                return;
            }
        }
    }

    heap_lock_release(irq_flags);
}

void *krealloc(void *ptr, u64 new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    heap_block_t *b = (heap_block_t*)((u8*)ptr - sizeof(heap_block_t));
    if (b->magic != HEAP_MAGIC) return NULL;

    /* If the new size fits in the current block, just return. */
    new_size = (new_size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    if (new_size <= b->size) return ptr;

    /* Need a bigger block. Allocate new, copy, free old. */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    oc_memcpy(new_ptr, ptr, b->size);
    kfree(ptr);
    return new_ptr;
}

void heap_get_stats(heap_stats_t *out) {
    if (!out) return;
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    out->heap_size = g_heap_size;
    out->allocated = g_allocated;
    out->free = g_heap_size - g_allocated - g_overhead;
    out->overhead = g_overhead;
    out->alloc_count = g_alloc_count;
    out->free_count = g_free_count;
    out->total_allocs = g_total_allocs;
    out->total_frees = g_total_frees;
    if (!(flags & 0x200)) __asm__ volatile("sti");
}

void heap_register_allocator_hook(heap_hook_fn fn) {
    g_hook = fn;
}
