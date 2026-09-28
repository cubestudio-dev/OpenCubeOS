/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03
 * File: kernel/vmm.c
 * Purpose: Virtual Memory Manager implementation.
 *
 * Manages 4-level page tables (PML4 -> PDPT -> PD -> PT). The kernel
 * address space is the boot-time identity mapping (first 4 GiB, set up
 * in boot.S). VMM can create new address spaces and manipulate pages
 * within them.
 */
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "exceptions.h"
#include "idt.h"

/* Page table structure constants. */
#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PTE_FLAGS_MASK 0xFFF0000000000FFFULL

/* Kernel virtual region: we map the kernel into every address space at
 * the same location (identity-mapped first 4 GiB from boot.S). For new
 * address spaces, we copy the kernel PML4 entries (entries 0-3 cover the
 * first 4 GiB).
 *
 * P4 fix: the old comment claimed "We also map entries 256-259 (the
 * higher-half aliases)" — but the kernel never sets PML4[256..259].
 * Only PML4[0..3] are populated (4 GiB identity mapping for the
 * low half). The kernel runs from the low-half identity mapping only. */

static vmm_fault_stats_t g_fault_stats;
static vmm_fault_handler_fn g_fault_handlers[4];
static int g_fault_handler_count = 0;

/* CR3 of the kernel address space (the boot-time PML4). */
static u64 g_kernel_pml4 = 0;

/* Heap virtual region: [0xC0000000, 0xC0400000) = 4 MiB.
 * Pages are allocated from PMM and mapped here on demand. */
#define HEAP_VSTART  0xC0000000ULL
#define HEAP_VEND    0xC0400000ULL
#define HEAP_SIZE    (HEAP_VEND - HEAP_VSTART)

/* Stack growth region: the kernel stack grows down from __boot_stack_top.
 * We allow growth within 64 KiB below the current stack. */

static inline void invlpg(u64 vaddr) {
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

static inline u64 read_cr3(void) {
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(u64 v) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory");
}

static inline u64 read_cr2(void) {
    u64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

void vmm_init(void) {
    oc_memset(&g_fault_stats, 0, sizeof(g_fault_stats));
    oc_memset(g_fault_handlers, 0, sizeof(g_fault_handlers));
    g_fault_handler_count = 0;
    g_kernel_pml4 = read_cr3() & PTE_ADDR_MASK;
}

vmm_as_t vmm_kernel_as(void) {
    return g_kernel_pml4;
}

vmm_as_t vmm_current_as(void) {
    return read_cr3() & PTE_ADDR_MASK;
}

void vmm_switch_as(vmm_as_t as) {
    write_cr3(as & PTE_ADDR_MASK);
}

/* Get a pointer to a PML4 entry. The PML4 is always at a known physical
 * address (identity-mapped in the kernel space). */
static volatile u64 *pml4_entry_ptr(vmm_as_t as, u64 vaddr) {
    u64 *pml4 = (u64*)(as & PTE_ADDR_MASK);
    return &pml4[PML4_INDEX(vaddr)];
}

/* Walk the page table hierarchy. If `create` is non-zero, allocate
 * intermediate tables as needed. Returns a pointer to the final PTE,
 * or NULL if the hierarchy doesn't exist and create is 0. */
/* P1-21: Note — if walk_pt fails partway through (pmm_alloc_frame returns 0),
 * the page tables already allocated are linked into the address space hierarchy
 * and will be freed by vmm_destroy_address_space. This is not a true leak. */
static volatile u64 *walk_pt(vmm_as_t as, u64 vaddr, int create) {
    volatile u64 *pml4e = pml4_entry_ptr(as, vaddr);
    if (!(*pml4e & VMM_FLAG_PRESENT)) {
        if (!create) return NULL;
        u64 pt_phys = pmm_alloc_frame();
        if (pt_phys == 0) return NULL;
        /* Zero the new page table (identity-mapped, so we can write to it). */
        oc_memset((void*)pt_phys, 0, PMM_PAGE_SIZE);
        *pml4e = pt_phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    } else if (create) {
        /* Ensure U flag is set so user-mode can traverse. */
        *pml4e |= VMM_FLAG_USER;
    }

    u64 *pdpt = (u64*)(*pml4e & PTE_ADDR_MASK);
    volatile u64 *pdpte = &pdpt[PDPT_INDEX(vaddr)];
    if (!(*pdpte & VMM_FLAG_PRESENT)) {
        if (!create) return NULL;
        u64 pt_phys = pmm_alloc_frame();
        if (pt_phys == 0) return NULL;
        oc_memset((void*)pt_phys, 0, PMM_PAGE_SIZE);
        *pdpte = pt_phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    } else if (create) {
        /* Ensure U flag is set so user-mode can traverse. */
        *pdpte |= VMM_FLAG_USER;
    }

    u64 *pd = (u64*)(*pdpte & PTE_ADDR_MASK);
    volatile u64 *pde = &pd[PD_INDEX(vaddr)];
    if (!(*pde & VMM_FLAG_PRESENT)) {
        if (!create) return NULL;
        /* Use 4KB pages (not 2MB huge pages) for fine-grained mapping. */
        u64 pt_phys = pmm_alloc_frame();
        if (pt_phys == 0) return NULL;
        oc_memset((void*)pt_phys, 0, PMM_PAGE_SIZE);
        *pde = pt_phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    } else if (*pde & 0x80) {
        /* Huge page (2 MiB) — need to split it into 4K pages. */
        if (!create) return NULL;
        /* P0-9 FIX: 2 MiB huge-page physical base is bits 21..51, not 30..51.
         * The old mask 0x000FFFFFC0000000 only kept bits 30..51, so splitting
         * a huge page at a non-1GiB-aligned address remapped all 512 child
         * PTEs to the wrong physical memory. */
        u64 huge_phys = *pde & 0x000FFFFFFFE00000ULL;  /* 2 MiB aligned (bits 21..51) */
        u64 huge_flags = *pde & 0xFFF;  /* preserve low flags */
        u64 pt_phys = pmm_alloc_frame();
        if (pt_phys == 0) return NULL;
        u64 *new_pt = (u64*)pt_phys;
        oc_memset(new_pt, 0, PMM_PAGE_SIZE);
        for (int i = 0; i < 512; i++) {
            new_pt[i] = (huge_phys + (u64)i * PMM_PAGE_SIZE) | (huge_flags & ~0x80);
        }
        *pde = pt_phys | (huge_flags & ~0x80) | VMM_FLAG_USER;
        /* Flush TLB for this address range. */
        if (as == vmm_current_as()) {
            __asm__ volatile(
                "mov %%cr4, %%rax\n"
                "and $0xFFFFFFFFFFFFFF7F, %%rax\n"
                "mov %%rax, %%cr4\n"
                "or $0x80, %%rax\n"
                "mov %%rax, %%cr4\n"
                "mov %%cr3, %%rax\n"
                "mov %%rax, %%cr3\n"
                ::: "rax", "memory"
            );
        }
    }

    u64 *pt = (u64*)(*pde & PTE_ADDR_MASK);
    return &pt[PT_INDEX(vaddr)];
}

int vmm_map_page(vmm_as_t as, u64 vaddr, u64 paddr, u64 flags) {
    if (vaddr & 0xFFF) return -1;  /* not page-aligned */
    if (paddr & 0xFFF) return -1;

    volatile u64 *pte = walk_pt(as, vaddr, 1);
    if (!pte) return -1;

    *pte = (paddr & PTE_ADDR_MASK) | (flags & PTE_FLAGS_MASK);
    if (as == vmm_current_as()) invlpg(vaddr);
    return 0;
}

u64 vmm_unmap_page(vmm_as_t as, u64 vaddr) {
    volatile u64 *pte = walk_pt(as, vaddr, 0);
    if (!pte || !(*pte & VMM_FLAG_PRESENT)) return 0;

    /* P3-3 FIX: Return the FULL old PTE (address + flags) so callers
     * can inspect the PRESENT bit (and any other flags) to decide
     * whether to free the physical frame. The old code did
     * `*pte & PTE_ADDR_MASK` which stripped all flag bits, so the
     * PRESENT bit was always 0 in the return value — callers
     * checking `ret & VMM_FLAG_PRESENT` always saw 0 and never
     * freed the physical page (silent leak on every munmap). */
    u64 old = *pte;
    *pte = 0;
    if (as == vmm_current_as()) invlpg(vaddr);
    return old;
}

int vmm_protect_page(vmm_as_t as, u64 vaddr, u64 flags) {
    volatile u64 *pte = walk_pt(as, vaddr, 0);
    if (!pte || !(*pte & VMM_FLAG_PRESENT)) return -1;

    u64 paddr = *pte & PTE_ADDR_MASK;
    *pte = paddr | (flags & PTE_FLAGS_MASK);
    if (as == vmm_current_as()) invlpg(vaddr);
    return 0;
}

int vmm_is_mapped(vmm_as_t as, u64 vaddr, u64 *paddr_out) {
    volatile u64 *pte = walk_pt(as, vaddr, 0);
    if (!pte || !(*pte & VMM_FLAG_PRESENT)) return 0;
    if (paddr_out) *paddr_out = *pte & PTE_ADDR_MASK;
    return 1;
}

vmm_as_t vmm_create_address_space(void) {
    /* Allocate a new PML4. */
    u64 pml4_phys = pmm_alloc_frame();
    if (pml4_phys == 0) return 0;

    /* Zero it. */
    oc_memset((void*)pml4_phys, 0, PMM_PAGE_SIZE);

    /* Copy the kernel entries from the boot PML4. Entries 0-3 cover the
     * first 4 GiB (identity-mapped). This makes the kernel accessible
     * in the new address space. */
    u64 *new_pml4 = (u64*)pml4_phys;
    u64 *kern_pml4 = (u64*)g_kernel_pml4;
    for (int i = 0; i < 4; i++) {
        new_pml4[i] = kern_pml4[i];
    }

    return pml4_phys;
}

/* BUG-002 FIX (P0): Differentiate AS created by vmm_create_address_space
 * (plain AS — pml4[0..3] COPIED from kernel, so they point to kernel's
 * PDPTs, which are SHARED) vs AS created by create_user_address_space
 * (user AS — pml4[0] points to a NEW per-process PDPT, only pml4[1..3]
 * are shared with kernel).
 *
 * For plain AS (vmtest, etc.): pml4[0] == kern_pml4[0] — we must NOT
 * free the PDPT/PD0 subtree because those pages belong to the kernel.
 *   Just free the pml4 page itself.
 *
 * For user AS (user processes): pml4[0] != kern_pml4[0] — the PDPT and
 *   PD0 are per-process; safe to free them along with user pages and PTs.
 *
 * Without this distinction, running vmtest followed by `run hello` would
 * free the kernel's PD0, causing a triple fault on the next user-mode
 * context switch. */
void vmm_destroy_address_space(vmm_as_t as) {
    if (as == g_kernel_pml4) return;  /* don't destroy kernel space */
    if (as == 0) return;

    u64 *pml4 = (u64*)as;
    u64 *kern_pml4 = (u64*)g_kernel_pml4;

    /* If pml4[0] points to the kernel's PDPT (same physical address),
     * this is a plain AS — don't free the shared PDPT subtree. */
    int is_plain_as = (pml4[0] == kern_pml4[0]);

    if (!is_plain_as && (pml4[0] & VMM_FLAG_PRESENT)) {
        u64 pdpt_phys = pml4[0] & PTE_ADDR_MASK;
        u64 *pdpt = (u64*)pdpt_phys;

        /* Walk PDPT[0] only (0-1GB, user space). PDPT[1..3] are shared. */
        if (pdpt[0] & VMM_FLAG_PRESENT) {
            u64 pd0_phys = pdpt[0] & PTE_ADDR_MASK;
            u64 *pd0 = (u64*)pd0_phys;

            for (int k = 0; k < 512; k++) {
                if (!(pd0[k] & VMM_FLAG_PRESENT)) continue;

                if (pd0[k] & 0x80) {
                    /* Huge page (2 MiB = 512 × 4K pages).
                     * Only free if it has USER flag (user-owned).
                     * Kernel huge pages (no USER) are skipped. */
                    if (pd0[k] & VMM_FLAG_USER) {
                        u64 huge_phys = pd0[k] & 0x000FFFFFFFE00000ULL;
                        for (int hp = 0; hp < 512; hp++) {
                            pmm_free_frame(huge_phys + (u64)hp * PMM_PAGE_SIZE);
                        }
                    }
                    continue;
                }

                /* Regular page table: per-user. Walk and free user pages. */
                u64 pt_phys = pd0[k] & PTE_ADDR_MASK;
                u64 *pt = (u64*)pt_phys;

                for (int l = 0; l < 512; l++) {
                    if (!(pt[l] & VMM_FLAG_PRESENT)) continue;
                    /* Only free pages with USER flag (kernel identity-mapped
                     * pages without USER belong to the kernel). */
                    if (pt[l] & VMM_FLAG_USER) {
                        u64 page = pt[l] & PTE_ADDR_MASK;
                        pmm_free_frame(page);
                    }
                }
                /* Free the PT frame (per-process). */
                pmm_free_frame(pt_phys);
            }
            /* Free PD0 frame (per-process copy of kernel PD0). */
            pmm_free_frame(pd0_phys);
        }
        /* Free PDPT frame (per-process). PDPT[1..3] entries point to
         * shared kernel PDs but the PDPT page itself is per-process. */
        pmm_free_frame(pdpt_phys);
    }

    /* Free the PML4 frame (per-process). PML4[1..3] are shared kernel
     * entries — we don't free the PDPTs they point to, just the PML4
     * page itself. This is safe for both plain and user AS. */
    pmm_free_frame(as);
}

void vmm_register_fault_handler(vmm_fault_handler_fn fn) {
    if (!fn) return;
    if (g_fault_handler_count < 4) {
        g_fault_handlers[g_fault_handler_count++] = fn;
    }
}

void vmm_get_fault_stats(vmm_fault_stats_t *out) {
    if (!out) return;
    /* Disable interrupts to read atomically. */
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    *out = g_fault_stats;
    if (!(flags & 0x200)) __asm__ volatile("sti");
}

/* Check if a virtual address is near the faulting stack pointer (stack growth).
 * P0-10 FIX: takes the faulting RSP (from the exception frame) instead of
 * reading the current RSP, which during a #PF points at the kernel exception
 * stack, not the user/kernel stack that actually faulted. */
static int in_stack_region(u64 vaddr, u64 faulting_rsp) {
    /* Allow growth within 64 KiB below the faulting RSP. */
    return vaddr < faulting_rsp && vaddr >= (faulting_rsp - 0x10000);
}

int vmm_handle_page_fault(u64 vaddr, u64 error_code, u64 rip, u64 faulting_rsp) {
    (void)error_code;
    g_fault_stats.total_faults++;

    /* Try L1 fault handlers first. */
    for (int i = 0; i < g_fault_handler_count; i++) {
        if (g_fault_handlers[i]) {
            if (g_fault_handlers[i](vaddr, error_code, rip)) {
                g_fault_stats.legal_faults++;
                return 1;  /* handled, resume */
            }
        }
    }

    /* Default handler: determine if this is a legal or illegal fault. */

    /* P2-17: heap region lazy allocation removed — heap uses identity-mapped
     * physical pages (heap.c allocates via pmm_alloc_frame), not the
     * [0xC0000000, 0xC0400000) virtual region. This was dead code. */

    /* Stack growth: if the fault is just below the faulting RSP, grow it. */
    if (in_stack_region(vaddr, faulting_rsp)) {
        u64 page = pmm_alloc_frame();
        if (page != 0) {
            /* BUG-030 FIX: Use USER flags (not KERNEL) for stack growth.
             * Old code used VMM_FLAGS_KERNEL which maps supervisor-only.
             * If a ring-3 user program touches the stack guard zone,
             * the #PF maps a kernel-only page, the user retries, and
             * faults again → infinite loop leaking pages until OOM.
             * Now we check if the fault came from ring-3 (user mode)
             * and use USER flags accordingly. */
            u64 flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE;
            /* Check if the faulting RIP is in user space (ring-3). */
            if (rip < 0x0000800000000000ULL) {
                flags |= VMM_FLAG_USER;  /* user-mode fault: map user-accessible */
            }
            if (vmm_map_page(vmm_current_as(), vaddr & ~0xFFF, page,
                             flags) == 0) {
                g_fault_stats.legal_faults++;
                g_fault_stats.stack_growth++;
                return 1;  /* resume */
            }
            pmm_free_frame(page);
        }
    }

    /* Illegal fault. */
    g_fault_stats.illegal_faults++;
    return 0;  /* let the exception dispatcher print + halt */
}
