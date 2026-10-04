/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03
 * File: kernel/vmm.h
 * Purpose: Virtual Memory Manager - 4-level page table manipulation.
 *
 * The kernel runs with an identity mapping of the first 4 GiB (set up by
 * boot.S). VMM provides APIs to create additional address spaces and
 * manipulate page tables within them.
 *
 * Page protection flags:
 *   VMM_FLAG_PRESENT  - page is mapped
 *   VMM_FLAG_WRITE    - writable
 *   VMM_FLAG_USER     - user-accessible (ring 3)
 *   VMM_FLAG_EXEC     - executable (NX bit clear)
 *   VMM_FLAG_COW      - copy-on-write (software flag)
 */
#ifndef OC_VMM_H
#define OC_VMM_H

#include "types.h"

/* Page table entry flags. */
#define VMM_FLAG_PRESENT  0x001
#define VMM_FLAG_WRITE    0x002
#define VMM_FLAG_USER     0x004
#define VMM_FLAG_EXEC     0x000  /* exec is default (NX=0); use VMM_FLAG_NOEXEC to disable */
#define VMM_FLAG_NOEXEC   0x8000000000000000ULL  /* NX bit (bit 63) */
#define VMM_FLAG_COW      0x200  /* software flag (reserved bit) */
#define VMM_FLAG_DIRTY    0x040
#define VMM_FLAG_ACCESSED 0x020

/* Default kernel page flags: present + writable. */
#define VMM_FLAGS_KERNEL (VMM_FLAG_PRESENT | VMM_FLAG_WRITE)
/* Default user page flags: present + writable + user. */
#define VMM_FLAGS_USER   (VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER)

/* Address space handle. 0 = kernel address space (identity-mapped). */
typedef u64 mem_vmm_as_t;

/* Page fault stats. */
typedef struct mem_vmm_fault_stats {
    u64 total_faults;
    u64 legal_faults;     /* handled successfully (stack growth, etc.) */
    u64 illegal_faults;   /* unhandled (bad access) */
    u64 stack_growth;     /* stack pages grown */
    u64 mem_heap_growth;      /* heap pages grown */
    u64 cow_faults;       /* copy-on-write faults */
} mem_vmm_fault_stats_t;

/* Initialize VMM (called after PMM). Sets up the kernel address space. */
void mem_vmm_init(void);

/* Get the kernel address space handle (the boot-time identity mapping). */
mem_vmm_as_t mem_vmm_kernel_as(void);

/* Create a new address space. Returns a PML4 physical address, or 0 on failure.
 * The new space has the kernel region mapped (so kernel code is accessible)
 * but user space is empty. */
mem_vmm_as_t mem_vmm_create_address_space(void);

/* Destroy an address space and free all its page tables.
 * Do NOT call this on the kernel address space. */
void mem_vmm_destroy_address_space(mem_vmm_as_t as);

/* Map a virtual page to a physical page in the given address space.
 * flags is a bitmask of VMM_FLAG_*.
 * Returns 0 on success, -1 on failure. */
int mem_vmm_map_page(mem_vmm_as_t as, u64 vaddr, u64 paddr, u64 flags);

/* Unmap a virtual page. Returns the physical address that was mapped, or 0. */
u64 mem_vmm_unmap_page(mem_vmm_as_t as, u64 vaddr);

/* Change protection flags of a mapped page.
 * Returns 0 on success, -1 if the page wasn't mapped. */
int mem_vmm_protect_page(mem_vmm_as_t as, u64 vaddr, u64 flags);

/* Query whether a virtual address is mapped. Returns 1 if mapped, 0 if not.
 * If paddr_out is non-NULL, stores the physical address. */
int mem_vmm_is_mapped(mem_vmm_as_t as, u64 vaddr, u64 *paddr_out);

/* Switch to the given address space (loads CR3). */
void mem_vmm_switch_as(mem_vmm_as_t as);

/* Get the current address space (reads CR3). */
mem_vmm_as_t mem_vmm_current_as(void);

/* Register a page fault handler. Called BEFORE the default handler.
 * Return 1 = handled (resume), 0 = let L0 handle. */
typedef int (*mem_vmm_fault_handler_fn)(u64 vaddr, u64 error_code, u64 rip);
void mem_vmm_register_fault_handler(mem_vmm_fault_handler_fn fn);

/* Get fault statistics. */
void mem_vmm_get_fault_stats(mem_vmm_fault_stats_t *out);

/* Called by the exception dispatcher when a #PF occurs.
 * Returns 1 if the fault was handled (resume), 0 if illegal (halt). */
int mem_vmm_handle_page_fault(u64 vaddr, u64 error_code, u64 rip, u64 faulting_rsp);

#endif /* OC_VMM_H */
