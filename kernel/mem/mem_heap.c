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
#include "mem_heap.h"
#include "mem_pmm.h"
#include "lib_string.h"

#define HEAP_MAGIC      0xDEADBEEFCAFEBABEULL
#define HEAP_MAGIC_FREE 0xFEEDFACE12345678ULL
#define HEAP_MIN_POOL_PAGES 16   /* 64 KiB initial pool (shell needs ~40KB for tokens) */
#define HEAP_MAX_POOL_PAGES 4096 /* BUG-0141 FIX (A1-8b): was 1024 (4 MiB).
                                  * Single contiguous allocations up to 16 MiB now
                                  * succeed (a 256 MiB FAT32 volume needs a ~2 MiB FAT
                                  * cache; keep headroom). Requests beyond the
                                  * single-pool ceiling fail fast without burning
                                  * a pool slot. */
#define HEAP_ALIGN 16
/* BUG-0140 FIX (A1-7): any size beyond the single-pool ceiling is rejected
 * before the alignment rounding, so a wrap-around size (>= 2^64 - 15)
 * can never round down to 0 and slip through the zero check. */
#define HEAP_MAX_ALLOC ((u64)HEAP_MAX_POOL_PAGES * PMM_PAGE_SIZE)

typedef struct mem_heap_block {
    u64 magic;           /* HEAP_MAGIC if allocated, HEAP_MAGIC_FREE if free */
    u64 size;            /* payload size in bytes (excludes header) */
    struct mem_heap_block *next;  /* next block in the free list (free blocks only) */
    struct mem_heap_block *prev;  /* prev block in the free list */
} mem_heap_block_t;
/* WP-10-AUDIT_P2-fix1: the struct is EXACTLY 32 bytes (4 x u64).  A fifth
 * `_pad` field used to make it 40 while the comment claimed 32, so every
 * try_split() advanced block addresses by 40 + 16k and block starts
 * ALTERNATED between 0 and 8 (mod 16).  Payloads (= block + sizeof) were
 * therefore 16-byte aligned only for even-parity blocks - alignment
 * depended on the allocation history.  DMA users that mask the payload
 * address (driver_block_ata_dma PRDT entry: addr & ~0xF) silently moved
 * their transfer window 8 bytes backwards whenever the bounce buffer came
 * from an odd-parity block (ATA-DMA reads shifted by 8 -> "fat32: invalid
 * BPB" on perfectly valid volumes, /etc unmountable, sshd without its
 * per-installation host key).  With a 32-byte header and 16-byte-rounded
 * requests, block starts and payloads are ALWAYS 16-byte aligned. */

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
static mem_heap_block_t *g_free_list = NULL;
static u64 g_heap_size = 0;
static u64 g_allocated = 0;
static u64 g_overhead = 0;
static u64 g_alloc_count = 0;
static u64 g_free_count = 0;
static u64 g_total_allocs = 0;
static u64 g_total_frees = 0;
static mem_heap_hook_fn g_hook = NULL;

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

static void add_pool_region(u64 base, u64 pages) {
    /* One big contiguous free block spanning all pages (BUG-0141 FIX:
     * shared by the contig path and the adjacent-run fallback path). */
    mem_heap_block_t *blk = (mem_heap_block_t*)base;
    blk->magic = HEAP_MAGIC_FREE;
    blk->size = pages * PMM_PAGE_SIZE - sizeof(mem_heap_block_t);
    blk->prev = NULL;
    blk->next = g_free_list;
    if (g_free_list) g_free_list->prev = blk;
    g_free_list = blk;
    g_heap_size += pages * PMM_PAGE_SIZE;
    g_overhead += sizeof(mem_heap_block_t);
    g_free_count++;
    if (g_pool_count < 16) {
        g_pool_pages[g_pool_count] = pages;
        g_pool_bases[g_pool_count] = base;
        g_pool_count++;
    }
}

static void add_pool(u64 page_count) {
    u64 phys = mem_pmm_alloc_contig(page_count);
    if (phys == 0) {
        /* Fallback: allocate single pages, but chain PHYSICALLY ADJACENT
         * pages into one contiguous block (BUG-0141 FIX, A1-8a). The old
         * fallback registered every page as base=0 "single-page pools"
         * that in_same_pool() could never merge - so under fragmentation
         * the heap could never satisfy the > 4 KB request that triggered
         * the growth, even when PMM still had free pages (allocated
         * sequentially, hence physically adjacent in practice). */
        u64 run_base = 0, run_pages = 0;
        for (u64 i = 0; i < page_count; i++) {
            u64 p = mem_pmm_alloc_frame();
            if (p == 0) break;
            if (run_pages && p == run_base + run_pages * PMM_PAGE_SIZE) {
                run_pages++;
            } else {
                if (run_pages) add_pool_region(run_base, run_pages);
                run_base = p;
                run_pages = 1;
            }
        }
        if (run_pages) add_pool_region(run_base, run_pages);
        /* BUG-0141 FIX (A1-8c): a fallback that got no page at all adds no
         * block and consumes no pool slot - 16 empty slots used to
         * permanently disable heap growth even after PMM freed memory
         * again. */
        return;
    }
    add_pool_region(phys, page_count);
}

void mem_heap_init(void) {
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
static mem_heap_block_t *find_free(u64 size) {
    for (mem_heap_block_t *b = g_free_list; b; b = b->next) {
        if (b->magic == HEAP_MAGIC_FREE && b->size >= size) {
            return b;
        }
    }
    return NULL;
}

/* Remove a block from the free list. */
static void remove_free(mem_heap_block_t *b) {
    if (b->prev) b->prev->next = b->next;
    else         g_free_list = b->next;
    if (b->next) b->next->prev = b->prev;
    b->next = NULL;
    b->prev = NULL;
    g_free_count--;
}

/* Add a block to the free list. */
static void add_free(mem_heap_block_t *b) {
    b->next = g_free_list;
    b->prev = NULL;
    if (g_free_list) g_free_list->prev = b;
    g_free_list = b;
    g_free_count++;
}

/* Try to split a block if it's much larger than needed. */
static void try_split(mem_heap_block_t *b, u64 needed) {
    /* Only split if the remainder can hold a header + at least 16 bytes. */
    if (b->size < needed + sizeof(mem_heap_block_t) + HEAP_ALIGN) return;

    mem_heap_block_t *new_blk = (mem_heap_block_t*)((u8*)b + sizeof(mem_heap_block_t) + needed);
    new_blk->magic = HEAP_MAGIC_FREE;
    new_blk->size = b->size - needed - sizeof(mem_heap_block_t);
    new_blk->next = NULL;
    new_blk->prev = NULL;

    b->size = needed;
    add_free(new_blk);
    g_overhead += sizeof(mem_heap_block_t);
}

/* P2-22 FIX: Real spinlock for the heap. Uses cli/sti to prevent IRQ
 * preemption (timer IRQ calls kmalloc from soft_timer callbacks).
 * This prevents free-list corruption when kmalloc is called from
 * both kernel code and an IRQ handler concurrently. */
static volatile u64 g_heap_lock = 0;

static inline void mem_heap_lock_acquire(u64 *flags) {
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(*flags));
    while (__atomic_test_and_set(&g_heap_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static inline void mem_heap_lock_release(u64 flags) {
    __atomic_clear(&g_heap_lock, __ATOMIC_RELEASE);
    __asm__ volatile("pushq %0; popfq" : : "r"(flags));
}

void *kmalloc(u64 size) {
    if (size == 0) return NULL;

    /* BUG-0140 FIX (A1-7): reject sizes that would wrap through the
     * alignment rounding. size >= 2^64 - 15 used to round to 0, pass the
     * zero check, make find_free(0) match the first free block (b->size
     * >= 0 is always true) and hand out a 0-size block whose caller would
     * then write the full unaligned length - heap overflow + stats drift. */
    if (size > HEAP_MAX_ALLOC) return NULL;

    /* Round up to alignment. */
    size = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    if (size == 0) return NULL;  /* paranoia: re-check after rounding */

    u64 irq_flags;
    mem_heap_lock_acquire(&irq_flags);

    mem_heap_block_t *b = find_free(size);
    if (!b) {
        /* Expand the heap. */
        u64 pages_needed = (size + sizeof(mem_heap_block_t) + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
        if (pages_needed < 4) pages_needed = 4;
        /* BUG-0141 FIX (A1-8b): beyond the single-pool ceiling, fail fast
         * instead of clamping to the ceiling and allocating a pool that
         * can never satisfy this request (the clamp also burned a pool
         * slot and left the heap fragmented for nothing). */
        if (pages_needed > HEAP_MAX_POOL_PAGES) { mem_heap_lock_release(irq_flags); return NULL; }
        add_pool(pages_needed);
        b = find_free(size);
        if (!b) { mem_heap_lock_release(irq_flags); return NULL; }
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

    if (g_hook) g_hook((u8*)b + sizeof(mem_heap_block_t), size, 1);

    mem_heap_lock_release(irq_flags);
    return (u8*)b + sizeof(mem_heap_block_t);
}

void *kzalloc(u64 size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;

    u64 irq_flags;
    mem_heap_lock_acquire(&irq_flags);

    mem_heap_block_t *b = (mem_heap_block_t*)((u8*)ptr - sizeof(mem_heap_block_t));

    /* Double-free detection. */
    if (b->magic == HEAP_MAGIC_FREE) {
        /* Double free! Don't crash, just ignore. */
        /* WP-09-FIX BUG-001: was bare return, leaking mem_heap_lock. */
        mem_heap_lock_release(irq_flags);
        return;
    }
    if (b->magic != HEAP_MAGIC) {
        /* Not a valid heap block. Ignore. */
        /* WP-09-FIX BUG-001: was bare return, leaking mem_heap_lock. */
        mem_heap_lock_release(irq_flags);
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
     * the pool was a contiguous multi-page run (WP-07's mem_pmm_alloc_contig).
     * The fix is to check if both blocks are in the same CONTIGUOUS POOL
     * rather than the same PAGE. Since we don't track pool bases, we use
     * the heuristic: if two blocks are exactly adjacent in VA, they're
     * in the same pool (the heap never allocates blocks that span pool
     * boundaries). This is safe because mem_pmm_alloc_contig returns
     * physically-contiguous pages that are also VA-contiguous.
     *
     * However, to avoid any risk of cross-pool corruption (two separate
     * single-page pools that happen to be VA-adjacent), we keep the
     * same-page check for single-page pools and only allow cross-page
     * coalescing when both blocks are in the same 16-page-or-larger pool.
     * Since we don't track pool membership, we conservatively require
     * same-page for now. A future fix could add pool-base tracking. */
    u64 block_start = (u64)(uintptr_t)b;
    u64 block_end = block_start + sizeof(mem_heap_block_t) + b->size;
    u64 page_start = block_start & ~(PMM_PAGE_SIZE - 1);

    /* P7 fix: Backward coalescing. The old code had a critical bug:
     * the return after backward coalesce did NOT call mem_heap_lock_release,
     * leaving the heap spinlock locked. The next kmalloc/kfree would
     * spin forever on mem_heap_lock_acquire → heaptest hung after the 2nd
     * kfree (which triggered the first backward coalesce). */

    /* Walk the free list looking for a block that ends right where we start. */
    for (mem_heap_block_t *f = g_free_list; f; f = f->next) {
        if (f->magic != HEAP_MAGIC_FREE) continue;
        u64 f_end = (u64)(uintptr_t)f + sizeof(mem_heap_block_t) + f->size;
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
                f->size += sizeof(mem_heap_block_t) + b->size;
                g_overhead -= sizeof(mem_heap_block_t);
                mem_heap_lock_release(irq_flags);  /* P7 FIX: was missing! */
                return;
            }
        }
    }

    /* Forward coalescing — check if the block AFTER us is free. */
    for (mem_heap_block_t *f = g_free_list; f; f = f->next) {
        if (f->magic != HEAP_MAGIC_FREE) continue;
        u64 f_start = (u64)(uintptr_t)f;
        if (f_start == block_end) {
            int same_page = (f_start & ~(PMM_PAGE_SIZE - 1)) == page_start;
            int same_pool = in_same_pool(f_start, block_start);
            if (same_page || same_pool) {
                /* Coalesce: merge f into b. */
                remove_free(f);
                b->size += sizeof(mem_heap_block_t) + f->size;
                g_overhead -= sizeof(mem_heap_block_t);
                mem_heap_lock_release(irq_flags);
                return;
            }
        }
    }

    mem_heap_lock_release(irq_flags);
}

void *krealloc(void *ptr, u64 new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    /* BUG-0140 FIX (A1-7): same integer-overflow class as kmalloc - the
     * alignment rounding below wraps sizes >= 2^64 - 15 to 0, which then
     * took the "fits in the current block" path and returned the OLD
     * pointer while the caller believes it holds new_size bytes (silent
     * buffer overflow on the caller's side). Reject over-ceiling sizes
     * exactly like kmalloc does. */
    if (new_size > HEAP_MAX_ALLOC) return NULL;

    mem_heap_block_t *b = (mem_heap_block_t*)((u8*)ptr - sizeof(mem_heap_block_t));
    if (b->magic != HEAP_MAGIC) return NULL;

    /* If the new size fits in the current block, just return. */
    new_size = (new_size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    if (new_size <= b->size) return ptr;

    /* Need a bigger block. Allocate new, copy, free old. */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, b->size);
    kfree(ptr);
    return new_ptr;
}

void mem_heap_get_stats(mem_heap_stats_t *out) {
    if (!out) return;
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    out->mem_heap_size = g_heap_size;
    out->allocated = g_allocated;
    out->free = g_heap_size - g_allocated - g_overhead;
    out->overhead = g_overhead;
    out->alloc_count = g_alloc_count;
    out->free_count = g_free_count;
    out->total_allocs = g_total_allocs;
    out->total_frees = g_total_frees;
    if (!(flags & 0x200)) __asm__ volatile("sti");
}

void mem_heap_register_allocator_hook(mem_heap_hook_fn fn) {
    g_hook = fn;
}
