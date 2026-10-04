/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03
 * File: kernel/heap.h
 * Purpose: Kernel heap - kmalloc/kfree/krealloc/kzalloc.
 *
 * The heap uses a free-list allocator with first-fit and block coalescing.
 * It backs itself with physical pages from PMM (which are identity-mapped
 * in the kernel address space, so no VMM mapping is needed).
 */
#ifndef OC_HEAP_H
#define OC_HEAP_H

#include "types.h"

typedef struct mem_heap_stats {
    u64 mem_heap_size;      /* total bytes managed */
    u64 allocated;      /* bytes currently allocated (payload only) */
    u64 free;           /* bytes available for allocation */
    u64 overhead;       /* block headers + alignment padding */
    u64 alloc_count;    /* number of outstanding allocations */
    u64 free_count;     /* number of free blocks */
    u64 total_allocs;   /* cumulative allocation count */
    u64 total_frees;    /* cumulative free count */
} mem_heap_stats_t;

/* Initialize the kernel heap. Allocates an initial pool from PMM. */
void mem_heap_init(void);

/* Allocate `size` bytes. Returns a page-aligned-or-better pointer, or NULL. */
void *kmalloc(u64 size);

/* Allocate `size` bytes and zero them. */
void *kzalloc(u64 size);

/* Free a previously allocated pointer. Detects double-free. */
void kfree(void *ptr);

/* Resize an allocation. May move the data. Returns the new pointer. */
void *krealloc(void *ptr, u64 new_size);

/* Query heap statistics. */
void mem_heap_get_stats(mem_heap_stats_t *out);

/* Register an allocator hook. Called on every alloc/free with the pointer
 * and size. Useful for debugging / leak tracking. */
typedef void (*mem_heap_hook_fn)(void *ptr, u64 size, int is_alloc);
void mem_heap_register_allocator_hook(mem_heap_hook_fn fn);

#endif /* OC_HEAP_H */
