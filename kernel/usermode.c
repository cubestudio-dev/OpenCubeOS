/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-04/WP-08a
 * File: kernel/usermode.c
 * Purpose: Userspace support - ring 3, syscalls, ELF loader, process table.
 */
#include "usermode.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "idt.h"
#include "fb.h"
#include "multiboot2.h"
#include "userprogs_data.h"
#include "solib_data.h"
#include "sched.h"

/* Syscall table. */
#define MAX_SYSCALLS 256
static syscall_handler_fn g_syscalls[MAX_SYSCALLS];

/* User process table. (non-static for syscall.c) */
user_proc_t g_procs[MAX_USER_PROCS];
int g_next_pid = 1;
int g_usershell_running = 0;  // WP-08cd: set by cmd_run when ush starts  /* BUG-047: non-static for fork() access */

/* WP-08a: saved interrupt frame pointer (for fork). */
static u64 *g_current_frame = NULL;

/* WP-08b Batch 2: embedded interpreter (ld.so) lookup table.
 *
 * When a dynamic ELF (ET_DYN) has a PT_INTERP program header, the kernel
 * reads the interpreter path string (e.g., "/lib/ld.so") and looks it up
 * in this table. The matching entry provides an embedded ELF blob that
 * the kernel maps into the user address space and jumps to.
 *
 * For Batch 2 the table has one entry: "/lib/ld.so" → userprog_ld_so (a
 * minimal test program that prints "ld.so started" and exits). Later
 * batches will replace this with a real ld.so implementation. */
extern const u8 userprog_ld_so[];
extern const u64 userprog_ld_so_size;

/* WP-08b Batch 3: the kernel maps the main program's PT_LOAD segments
 * at MAIN_PROG_BASE so that ld.so can read the main ELF's .dynamic
 * section from a fixed address. The main program is a PIE (ET_DYN) so
 * its p_vaddr values are relative to load base 0; we map each PT_LOAD
 * at MAIN_PROG_BASE + p_vaddr, which puts the in-memory image at
 * 0x20000000 (load base) + p_vaddr.
 *
 * 0x20000000 = 512 MB is chosen to not collide with:
 *   - ld.so at 0x10000000 (256 MB)
 *   - heap region around 0x500000 (5 MB, USER_BRK_BASE)
 *   - user stack at 0x40000000 (1 GB, USER_STACK_TOP)
 *   - the main program's own PT_LOAD would have been at low vaddrs
 *     (0, 0x1000, etc.) but we OFFSET them by MAIN_PROG_BASE. */
#define MAIN_PROG_BASE 0x20000000ULL

/* WP-08b Batch 4a: the kernel maps the embedded libfoo.so (from
 * solib_data.h) to SOLIB_LIBFOO_BASE so ld.so can read its ELF header,
 * .dynamic, .dynsym, .dynstr from a fixed address.
 *
 * 0x30000000 = 768 MB is above main program @ 0x20000000 and below
 * user stack @ 0x40000000. Doesn't collide with anything.
 *
 * For Batch 4a the kernel always maps libfoo.so unconditionally when
 * loading an ET_DYN ELF (since it's the only embedded .so for now).
 * Later batches will let ld.so decide which .so to load (via DT_NEEDED
 * parsing → name → kernel syscall → map). */
#define SOLIB_LIBFOO_BASE 0x30000000ULL

struct interp_entry {
    const char *path;
    const u8 *elf_data;
    u64 elf_size;
};
static const struct interp_entry g_interp_table[] = {
    { "/lib/ld.so", userprog_ld_so, userprog_ld_so_size },
};
#define NUM_INTERP_ENTRIES (sizeof(g_interp_table) / sizeof(g_interp_table[0]))

/* interp_lookup: find an embedded ld.so ELF by its PT_INTERP path.
 * `path` may or may not be NUL-terminated; `path_len` is its max length
 * (typically the PT_INTERP p_filesz). Returns the ELF data pointer and
 * writes its size to *out_size, or returns NULL if no entry matches. */
static const u8 *interp_lookup(const char *path, u64 path_len, u64 *out_size) {
    for (u64 i = 0; i < NUM_INTERP_ENTRIES; i++) {
        const char *p = path;
        const char *q = g_interp_table[i].path;
        u64 plen = 0;
        /* Walk both strings while within bounds, both non-NUL, and equal. */
        while (plen < path_len && *p != '\0' && *q != '\0') {
            if (*p != *q) break;
            p++; q++; plen++;
        }
        /* Match: the table entry's path is fully consumed (at NUL), AND
         * the input is also at end (either at NUL or at path_len). */
        if (*q == '\0' && (plen == path_len || *p == '\0')) {
            *out_size = g_interp_table[i].elf_size;
            return g_interp_table[i].elf_data;
        }
    }
    return NULL;
}

/* ELF64 header structures (minimal). */
typedef struct {
    u8  ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} __attribute__((packed)) elf64_hdr_t;

typedef struct {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} __attribute__((packed)) elf64_phdr_t;

/* Forward declarations. */
u64 syscall_exit(u64 code, u64 arg2, u64 arg3, u64 arg4);
u64 syscall_write(u64 buf, u64 len, u64 arg3, u64 arg4);
u64 syscall_write_and_exit(u64 buf, u64 len, u64 arg3, u64 arg4);

void usermode_init(void) {
    oc_memset(g_syscalls, 0, sizeof(g_syscalls));
    oc_memset(g_procs, 0, sizeof(g_procs));
    g_next_pid = 1;
    g_current_frame = NULL;
    g_syscalls[0] = syscall_exit;
    g_syscalls[1] = syscall_write;
    g_syscalls[2] = syscall_write_and_exit;

    extern void syscall_wp08a_init(void);
    syscall_wp08a_init();
}

int syscall_register(int num, syscall_handler_fn handler) {
    if (num < 0 || num >= MAX_SYSCALLS || !handler) return -1;
    g_syscalls[num] = handler;
    return 0;
}

u64 syscall_write(u64 buf, u64 len, u64 arg3, u64 arg4) {
    (void)arg3; (void)arg4;
    const u64 USER_LIMIT = 0x0000800000000000ULL;
    if (buf >= USER_LIMIT) return (u64)-1;
    if (len > 65536) len = 65536;
    if (buf + len > USER_LIMIT) return (u64)-1;
    /* BUG-011 FIX: Check that the user buffer is actually mapped.
     * Old code only checked range, not mapping. If a user program
     * passes an unmapped pointer (e.g., 0x2000 in the NULL guard
     * zone), the kernel would #PF → halt (pre-BUG-001) or kill
     * (post-BUG-001). Now we return -1 gracefully. */
    user_proc_t *proc = user_process_current();
    if (proc && proc->as) {
        for (u64 a = buf & ~0xFFFULL; a < buf + len; a += 0x1000) {
            u64 phys;
            if (!vmm_is_mapped(proc->as, a, &phys)) return (u64)-1;
        }
    }
    const char *p = (const char*)buf;
    for (u64 i = 0; i < len; i++) oc_console_putc(p[i]);
    return len;
}

u64 syscall_write_and_exit(u64 buf, u64 len, u64 arg3, u64 arg4) {
    (void)arg3; (void)arg4;
    const u64 USER_LIMIT = 0x0000800000000000ULL;
    if (buf < USER_LIMIT && buf + len <= USER_LIMIT && len <= 65536) {
        /* P3-4 FIX: Add the same mapping check as syscall_write.
         * Old code only validated the address RANGE, not whether the
         * pages were actually mapped. If a user program passed an
         * unmapped pointer (e.g. NULL or a guard-zone address), the
         * kernel would #PF on the read → kill/halt. Now we check
         * vmm_is_mapped for every page in the buffer range first. */
        user_proc_t *proc = user_process_current();
        if (proc && proc->as) {
            for (u64 a = buf & ~0xFFFULL; a < buf + len; a += 0x1000) {
                u64 phys;
                if (!vmm_is_mapped(proc->as, a, &phys)) {
                    /* Unmapped pointer — exit silently with no output */
                    syscall_exit(0, 0, 0, 0);
                    return 0;
                }
            }
        }
        const char *p = (const char*)buf;
        for (u64 i = 0; i < len; i++) oc_console_putc(p[i]);
    }
    syscall_exit(0, 0, 0, 0);
    return 0;
}

u64 syscall_exit(u64 code, u64 arg2, u64 arg3, u64 arg4) {
    (void)arg2; (void)arg3; (void)arg4;
    tid_t tid = kthread_current_tid();
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].tid == tid) {
            g_procs[i].alive = 0;
            char n[8]; oc_u64_to_str((u64)g_procs[i].pid, n);
            oc_console_puts("[user] process ");
            oc_console_puts(n);
            oc_console_puts(" exited (code=");
            oc_u64_to_str(code, n);
            oc_console_puts(n);
            oc_console_puts(")\n");
            /* BUG-008 FIX: Destroy the user address space before exiting.
             * Switch to kernel CR3 first so we don't destroy the page
             * tables we're currently running on. */
            /* P1-9 FIX: The old code had an EMPTY if block here — the
             * vmm_destroy_address_space call was never added. This caused
             * all asm user programs (hello, badapp, loop, fork_test, etc.)
             * that use syscall 0 (SYS_EXIT) to leak their entire page table
             * hierarchy (~2.4 pages per exit). After 31 runs, used pages
             * grew from 564 to 639 (+75 pages leaked). */
            if (g_procs[i].as) {
                __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_as()) : "memory");
                vmm_destroy_address_space(g_procs[i].as);
                g_procs[i].as = 0;
            }
            break;
        }
    }
    task_t *t = kthread_current();
    if (t) t->state = TASK_EXITED;
    for (;;) kthread_block();
    return 0;
}

void syscall_dispatch(u64 *regs) {
    oc_irq_frame_t *f = (oc_irq_frame_t*)regs;
    u64 syscall_num = f->rax;
    u64 arg1 = f->rdi;
    u64 arg2 = f->rsi;
    u64 arg3 = f->rdx;
    u64 arg4 = regs[5];  /* r10 */

    /* WP-08a: save frame pointer for fork access */
    g_current_frame = regs;

    if (syscall_num < MAX_SYSCALLS && g_syscalls[syscall_num]) {
        f->rax = g_syscalls[syscall_num](arg1, arg2, arg3, arg4);
    } else {
        f->rax = (u64)-1;
    }

    /* P3-12 FIX: Signal delivery.
     * After the syscall returns (but before iretq), check if this
     * process has a pending signal with a registered handler. If so:
     *   1. Save the current IRQ frame (22 u64s) into sig_saved_frame.
     *   2. Modify the live frame: rip = handler, rdi = signal number,
     *      rsp = current user rsp - 128 (skip red zone) - 8 (fake
     *      return address for the handler's `ret`). Actually we just
     *      set rsp = current user rsp (the handler runs on the user
     *      stack — the handler itself uses normal call/ret with its
     *      own stack frame).
     *   3. Clear pending_signal.
     *   4. Set sig_in_progress = 1.
     * When the handler calls SYS_SIGRETURN (42), sys_sigreturn
     * restores sig_saved_frame → the original RIP/RSP/RFLAGS/
     * registers — and iretq returns the user to where they were
     * before the signal. */
    user_proc_t *proc = user_process_current();
    if (proc && proc->pending_signal > 0 && proc->pending_signal < NSIG) {
        int sig = proc->pending_signal;
        signal_handler_fn handler = proc->sig_handlers[sig];
        if (handler != NULL) {
            /* Save the 22 u64 frame (r15..ss). */
            u64 *src = regs;
            for (int i = 0; i < 22; i++)
                proc->sig_saved_frame[i] = src[i];
            proc->sig_in_progress = 1;
            proc->pending_signal = 0;
            /* Modify the live frame so iretq jumps to the handler. */
            f->rip = (u64)(uintptr_t)handler;
            f->rdi = (u64)sig;          /* first arg = signal number */
        }
    }

    g_current_frame = NULL;
}

void user_set_current_frame(u64 *frame_regs) { g_current_frame = frame_regs; }
u64 *user_get_current_frame(void) { return g_current_frame; }

/* P3-11 FIX: Unified resource reaper.
 *
 * Old code had THREE separate paths that "killed" a user process:
 *   1. sys_exit2 — closed pipe fds + destroyed AS ✓
 *   2. sys_kill (SIGKILL + default action) — set alive=0 + EXITED,
 *      but did NOT close fds or destroy AS ✗ (leaked PML4/PDPT/PD0/PT
 *      and any open VFS/pipe fds — pipe counts never decremented)
 *   3. exception kill — destroyed AS but did NOT close pipe fds ✗
 *      (pipe readers could block forever waiting for a writer that
 *      had been killed by an exception)
 *
 * Now all three call this single helper, which:
 *   - marks the process dead
 *   - sets the exit code
 *   - closes ALL pipe fds (decrements reader/writer counts, wakes waiters)
 *   - closes ALL VFS fds (vfs_close)
 *   - destroys the user AS (after switching to kernel CR3)
 *
 * Note: the caller is responsible for setting task->state = TASK_EXITED
 * and waking parent_tid (this helper doesn't know the calling context). */

/* Forward-declared helpers in syscall.c — keep the pipe struct private
 * (kernel_pipe_t is file-local in syscall.c). We just call these accessors
 * via their non-static exports. */
extern void pipe_close_fd(int pipe_id, int kind);  /* syscall.c */
extern void vfs_close(int fd);  /* vfs.h */

void user_process_reap_resources(user_proc_t *p, int exit_code) {
    if (!p) return;
    p->alive = 0;
    p->exit_code = exit_code;
    /* Close all fds (VFS + pipe). */
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        int kind = p->fds[i].kind;
        int pipe_id = p->fds[i].pipe_id;
        int vfs_fd = p->fds[i].vfs_fd;
        if (kind == 0) continue;
        p->fds[i].kind = 0;
        p->fds[i].pipe_id = 0;
        p->fds[i].vfs_fd = 0;
        if (kind == 2 || kind == 3) {
            pipe_close_fd(pipe_id, kind);
        } else if (kind == 1) {
            vfs_close(vfs_fd);
        }
    }
    /* Destroy the user AS. Switch to kernel CR3 first so we don't
     * destroy the page tables we're running on. */
    if (p->as) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_as()) : "memory");
        vmm_destroy_address_space(p->as);
        p->as = 0;
    }
}

user_proc_t *user_process_current(void) {
    tid_t tid = kthread_current_tid();
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].tid == tid) return &g_procs[i];
    }
    return NULL;
}

/* Build a user address space with per-process page tables.
 * WP-08a: Creates SEPARATE PDPT0 and PD0 for each address space. */
vmm_as_t create_user_address_space(void) {
    u64 pml4_phys = pmm_alloc_frame();
    if (pml4_phys == 0) return 0;
    oc_memset((void*)pml4_phys, 0, PMM_PAGE_SIZE);
    u64 *pml4 = (u64*)pml4_phys;

    u64 *kern_pml4 = (u64*)(vmm_kernel_as());

    /* Copy kernel PML4 entries 1-3 (supervisor-only, shared). */
    for (int i = 1; i < 4; i++) pml4[i] = kern_pml4[i];

    /* Create a separate PDPT for PML4[0] (0-512GiB) */
    u64 pdpt_phys = pmm_alloc_frame();
    if (pdpt_phys == 0) { pmm_free_frame(pml4_phys); return 0; }
    oc_memset((void*)pdpt_phys, 0, PMM_PAGE_SIZE);
    u64 *pdpt = (u64*)pdpt_phys;
    pml4[0] = pdpt_phys | 0x007;

    u64 *kern_pdpt = (u64*)(kern_pml4[0] & 0x000FFFFFFFFFF000ULL);

    /* Create a new PD for PDPT[0] (0-1GiB, where user code/stack live) */
    u64 pd0_phys = pmm_alloc_frame();
    if (pd0_phys == 0) { pmm_free_frame(pdpt_phys); pmm_free_frame(pml4_phys); return 0; }
    oc_memset((void*)pd0_phys, 0, PMM_PAGE_SIZE);
    u64 *pd0 = (u64*)pd0_phys;
    pdpt[0] = pd0_phys | 0x007;

    /* Copy kernel PD0 entries (512 x 2MiB huge pages, supervisor-only) */
    u64 *kern_pd0 = (u64*)(kern_pdpt[0] & 0x000FFFFFFFFFF000ULL);
    for (int i = 0; i < 512; i++) pd0[i] = kern_pd0[i];

    /* Copy PDPT[1..3] from kernel (1-4GiB, no user pages, safe to share) */
    for (int i = 1; i < 4; i++) pdpt[i] = kern_pdpt[i];

    /* P3-1 FIX (security): The old code split the 2MiB huge page at PD0[2]
     * (0x400000-0x600000) and PRE-POPULATED all 512 PT entries with
     * `base_phys + i*0x1000` — pointing them at the KERNEL's identity-
     * mapped physical pages. Although the flags lacked the USER bit
     * (so ring-3 couldn't read them directly), the kernel's physical
     * pages were still mapped into the user AS. Any future bug that
     * accidentally set USER on these PTEs would leak kernel memory.
     *
     * Fix: allocate an empty PT, leave all entries 0 (Not Present).
     * User memory allocators (map_user_pages, sys_brk, sys_mmap) call
     * vmm_map_page → walk_pt(create=1) which fills entries on demand
     * with NEW physical pages from pmm_alloc_frame.
     *
     * Kernel still has access to its 0x400000-0x600000 data via the
     * shared kernel PML4[1..3] identity mapping (1-4GiB region), and
     * via the un-split huge pages at PD0[0,1,3..511].
     *
     * WP-10a NOTE: because this window is no longer identity-mapped in
     * user address spaces, pmm_init() reserves the 0x400000-0x600000
     * PHYSICAL frames so that no page table, kernel stack, or physical
     * dereference target ever lands in the remapped window (see the
     * comment in pmm.c). */
    u64 pt_phys = pmm_alloc_frame();
    if (pt_phys == 0) {
        pmm_free_frame(pd0_phys); pmm_free_frame(pdpt_phys); pmm_free_frame(pml4_phys);
        return 0;
    }
    oc_memset((void*)pt_phys, 0, PMM_PAGE_SIZE);
    /* Replace the huge PDE with the empty PT pointer. P|W|U so the
     * walk can succeed for ring-0 in supervisor mode (USER bit on
     * intermediate tables is required so user code can traverse to
     * its own leaf PTEs which ARE User-flagged). */
    pd0[2] = pt_phys | 0x007;

    return pml4_phys;
}

/* WP-08a: Copy all user-accessible pages from src_as to a new address space. */
vmm_as_t copy_user_address_space(vmm_as_t src_as) {
    if (src_as == 0) return 0;
    vmm_as_t dst_as = create_user_address_space();
    if (dst_as == 0) return 0;

    u64 *src_pml4 = (u64*)src_as;
    u64 *dst_pml4 = (u64*)dst_as;

    for (int pml4_idx = 0; pml4_idx < 4; pml4_idx++) {
        u64 pml4e = src_pml4[pml4_idx];
        if (!(pml4e & VMM_FLAG_PRESENT)) continue;
        u64 *src_pdpt = (u64*)(pml4e & 0x000FFFFFFFFFF000ULL);
        u64 dst_pdpte = dst_pml4[pml4_idx];
        if (!(dst_pdpte & VMM_FLAG_PRESENT)) continue;
        u64 *dst_pdpt = (u64*)(dst_pdpte & 0x000FFFFFFFFFF000ULL);

        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
            u64 pdpte = src_pdpt[pdpt_idx];
            if (!(pdpte & VMM_FLAG_PRESENT)) continue;
            u64 *src_pd = (u64*)(pdpte & 0x000FFFFFFFFFF000ULL);
            u64 dst_pdpte2 = dst_pdpt[pdpt_idx];
            u64 *dst_pd;
            if (!(dst_pdpte2 & VMM_FLAG_PRESENT)) {
                u64 pd_phys = pmm_alloc_frame();
                if (pd_phys == 0) return dst_as;
                dst_pd = (u64*)pd_phys;
                oc_memset(dst_pd, 0, PMM_PAGE_SIZE);
                dst_pdpt[pdpt_idx] = pd_phys | 0x007;
            } else {
                dst_pd = (u64*)(dst_pdpte2 & 0x000FFFFFFFFFF000ULL);
            }

            for (int pd_idx = 0; pd_idx < 512; pd_idx++) {
                u64 pde = src_pd[pd_idx];
                if (!(pde & VMM_FLAG_PRESENT)) continue;
                if (pde & 0x80) continue;  /* skip huge pages */

                u64 *src_pt = (u64*)(pde & 0x000FFFFFFFFFF000ULL);
                u64 dst_pde = dst_pd[pd_idx];
                u64 *dst_pt;

                if (!(dst_pde & VMM_FLAG_PRESENT)) {
                    u64 pt_phys = pmm_alloc_frame();
                    if (pt_phys == 0) return dst_as;
                    dst_pt = (u64*)pt_phys;
                    oc_memset(dst_pt, 0, PMM_PAGE_SIZE);
                    dst_pd[pd_idx] = pt_phys | 0x007;
                } else if (dst_pde & 0x80) {
                    /* Split dst huge page into a PT */
                    u64 huge_phys = dst_pde & 0x000FFFFFFFE00000ULL;
                    u64 huge_flags = dst_pde & 0xFFF;
                    u64 pt_phys = pmm_alloc_frame();
                    if (pt_phys == 0) return dst_as;
                    dst_pt = (u64*)pt_phys;
                    oc_memset(dst_pt, 0, PMM_PAGE_SIZE);
                    for (int i = 0; i < 512; i++)
                        dst_pt[i] = (huge_phys + (u64)i * PMM_PAGE_SIZE) | (huge_flags & ~0x80ull);
                    dst_pd[pd_idx] = pt_phys | 0x007;
                } else {
                    dst_pt = (u64*)(dst_pde & 0x000FFFFFFFFFF000ULL);
                }

                for (int pt_idx = 0; pt_idx < 512; pt_idx++) {
                    u64 pte = src_pt[pt_idx];
                    if (!(pte & VMM_FLAG_PRESENT)) continue;
                    if (!(pte & VMM_FLAG_USER)) continue;
                    u64 src_phys = pte & 0x000FFFFFFFFFF000ULL;
                    u64 dst_phys = pmm_alloc_frame();
                    if (dst_phys == 0) return dst_as;
                    oc_memcpy((void*)dst_phys, (void*)src_phys, PMM_PAGE_SIZE);
                    u64 flags = pte & 0xFFF;
                    flags |= (pte & VMM_FLAG_NOEXEC);
                    dst_pt[pt_idx] = dst_phys | flags;
                }
            }
        }
    }
    return dst_as;
}

int map_user_pages(vmm_as_t as, u64 vaddr, const void *src, u64 size) {
    u64 offset = 0;
    while (offset < size) {
        u64 page_vaddr = (vaddr + offset) & ~0xFFFULL;
        u64 phys = pmm_alloc_frame();
        if (phys == 0) return -1;
        if (vmm_map_page(as, page_vaddr, phys,
                         VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER) != 0) {
            pmm_free_frame(phys);
            return -1;
        }
        u64 page_offset = (vaddr + offset) & 0xFFF;
        u64 copy_size = PMM_PAGE_SIZE - page_offset;
        if (copy_size > size - offset) copy_size = size - offset;
        oc_memcpy((u8*)phys + page_offset, (const u8*)src + offset, copy_size);
        offset += copy_size;
    }
    return 0;
}

void user_task_launcher(void *arg) {
    user_proc_t *proc = (user_proc_t*)arg;

    task_t *t = kthread_current();
    u64 kern_rsp = (u64)(uintptr_t)t->stack_base + t->stack_size;
    oc_set_tss_rsp0(kern_rsp);

    vmm_as_t user_as = proc->as;
    if (user_as == 0) {
        oc_console_puts("[user] no address space\n");
        kthread_block();
    }
    __asm__ volatile("mov %0, %%cr3\n" :: "r"(user_as) : "memory");

    if (proc->is_fork_child) {
        /* WP-08cd: Fork child — restore ALL callee-saved registers
         * from the saved IRQ frame, then IRETQ to user mode. */
        enter_ring3_fork(proc->fork_regs, user_as);
    } else {
        enter_ring3(proc->entry_point, proc->user_rsp, user_as);
    }

    for (;;) kthread_block();
}

void enter_ring3(u64 entry_point, u64 user_rsp, u64 user_cr3) {
    (void)user_cr3;
    __asm__ volatile(
        "mov $0x3B, %%ax\n"
        "push %%rax\n"
        "push %0\n"
        "pushf\n"
        "orl $0x200, (%%rsp)\n"
        "mov $0x33, %%ax\n"
        "push %%rax\n"
        "push %1\n"
        "mov $0x3B, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "iretq\n"
        :
        : "r"(user_rsp), "r"(entry_point)
        : "rax", "memory"
    );
}

pid_t user_process_create(const u8 *elf_data, u64 elf_size, const char *name) {
    if (!elf_data || elf_size < sizeof(elf64_hdr_t)) return -1;
    elf64_hdr_t *hdr = (elf64_hdr_t*)elf_data;
    if (hdr->ident[0] != 0x7f || hdr->ident[1] != 'E' ||
        hdr->ident[2] != 'L'  || hdr->ident[3] != 'F') {
        oc_console_puts("[user] not an ELF file\n");
        return -1;
    }
    if (hdr->ident[4] != 2) {
        oc_console_puts("[user] not ELF64\n");
        return -1;
    }

    /* WP-08b Batch 3: when loading a dynamic ELF, save the original
     * (main program) ELF data + size so we can map it to MAIN_PROG_BASE
     * after the user address space is created. ld.so reads the main
     * program's .dynamic section from there. */
    const u8 *main_elf_data_orig = NULL;
    u64 main_elf_size_orig = 0;

    /* WP-08b Batch 2: detect ET_DYN, parse PT_INTERP, look up ld.so,
     * and redirect the load path so the existing PT_LOAD loop below
     * maps the ld.so ELF (not the main program) and the entry point
     * becomes ld.so's _start.
     *
     * The kernel's job for ET_DYN (per HANDOFF.md):
     *   1. Recognize ET_DYN
     *   2. Parse PT_INTERP to find the ld.so path
     *   3. Map ld.so, jump to ld.so entry
     * Batch 2 does all three. The ld.so itself only prints a banner
     * and exits (real dynamic linking is later batches). */
    if (hdr->type == 3 /* ET_DYN */) {
        /* P4 fix: silenced success-path messages — was printing "ET_DYN detected",
         * "dynlink: dynamic ELF, requires ld.so", etc. on every dynamic load.
         * Now silent on success; only error messages are printed. */

        /* Parse program headers looking for PT_INTERP (type == 3). */
        elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);
        const char *interp_path = NULL;
        u64 interp_path_len = 0;
        for (int i = 0; i < hdr->phnum; i++) {
            if (phdr[i].type != 3 /* PT_INTERP */) continue;
            if (phdr[i].offset + phdr[i].filesz > elf_size) {
                oc_console_puts("PT_INTERP: (offset out of bounds)\n");
                return -1;
            }
            interp_path = (const char*)(elf_data + phdr[i].offset);
            interp_path_len = phdr[i].filesz;
            break;
        }
        if (!interp_path) {
            oc_console_puts("dynlink: PT_INTERP not present\n");
            return -1;
        }
        /* P4 fix: silenced — was printing the PT_INTERP path here. */

        /* Look up the embedded ld.so ELF by interpreter path. */
        u64 ldso_size = 0;
        const u8 *ldso_data = interp_lookup(interp_path, interp_path_len, &ldso_size);
        if (!ldso_data) {
            oc_console_puts("dynlink: no embedded ld.so for path\n");
            return -1;
        }
        /* Validate ld.so ELF header. */
        if (ldso_size < sizeof(elf64_hdr_t)) {
            oc_console_puts("dynlink: ld.so too small\n");
            return -1;
        }
        elf64_hdr_t *ldso_hdr = (elf64_hdr_t*)ldso_data;
        if (ldso_hdr->ident[0] != 0x7f || ldso_hdr->ident[1] != 'E' ||
            ldso_hdr->ident[2] != 'L'  || ldso_hdr->ident[3] != 'F') {
            oc_console_puts("dynlink: ld.so not an ELF file\n");
            return -1;
        }
        if (ldso_hdr->ident[4] != 2) {
            oc_console_puts("dynlink: ld.so not ELF64\n");
            return -1;
        }
        /* Find ld.so's first PT_LOAD vaddr — this is the "load base"
         * we report to the user. ld.so (ET_EXEC) is linked at 0x10000000. */
        elf64_phdr_t *ldso_phdr = (elf64_phdr_t*)(ldso_data + ldso_hdr->phoff);
        int has_load = 0;
        for (int i = 0; i < ldso_hdr->phnum; i++) {
            if (ldso_phdr[i].type == 1 /* PT_LOAD */) {
                has_load = 1;
                break;
            }
        }
        if (!has_load) {
            oc_console_puts("dynlink: ld.so has no PT_LOAD segment\n");
            return -1;
        }
        /* P4 fix: silenced — was printing "mapping ld.so at 0x...". */

        /* WP-08b Batch 3: save the main program ELF data + size so we
         * can map it to MAIN_PROG_BASE after the user address space is
         * created below. ld.so will read the main ELF's .dynamic section
         * from MAIN_PROG_BASE.
         *
         * We save BEFORE the redirect below, otherwise elf_data/elf_size
         * would point at ld.so's bytes instead of the main program's. */
        main_elf_data_orig = elf_data;
        main_elf_size_orig = elf_size;

        /* P4 fix: silenced — was printing "mapping main program ELF at 0x...". */

        /* WP-08b Batch 4a: the kernel also maps the embedded libfoo.so
         * to SOLIB_LIBFOO_BASE so ld.so can read its ELF header +
         * .dynamic + .dynsym + .dynstr from a fixed address.
         *
         * For Batch 4a the kernel always maps libfoo.so unconditionally
         * (it's the only embedded .so for now). Later batches will let
         * ld.so drive the loading via DT_NEEDED + name → syscall → map. */
        /* P4 fix: silenced — was printing "mapping libfoo.so at 0x...". */

        /* P4 fix: silenced — was printing "jumping to ld.so entry". */

        /* Redirect: from here on, load ld.so instead of the main program.
         * The PT_LOAD loop below will iterate ldso's program headers and
         * map them; proc->entry_point will be set to ldso_hdr->entry
         * (ld.so's _start). The main program is NOT loaded by the kernel
         * in Batch 2 — that becomes ld.so's job in later batches. */
        elf_data = ldso_data;
        elf_size = ldso_size;
        hdr = ldso_hdr;
    }

    int slot = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!g_procs[i].alive) { slot = i; break; }
    }
    if (slot < 0) return -1;

    user_proc_t *proc = &g_procs[slot];
    oc_memset(proc, 0, sizeof(*proc));
    proc->pid = g_next_pid++;
    proc->alive = 1;
    proc->next_solib_addr = USER_SOLIB_BASE;  /* WP-08b Batch 5: dlopen bump allocator */
    if (name) oc_strncpy(proc->name, name, 31);

    proc->as = create_user_address_space();
    if (proc->as == 0) { proc->alive = 0; return -1; }

    /* WP-08b Batch 3: if loading a dynamic ELF, map the main program's
     * PT_LOAD segments at MAIN_PROG_BASE so ld.so can read .dynamic.
     *
     * The main program is a PIE (ET_DYN), so its p_vaddr values are
     * relative to load base 0. We offset each PT_LOAD's vaddr by
     * MAIN_PROG_BASE so the in-memory image lives at 0x20000000 + p_vaddr.
     * ld.so reads the main ELF header at 0x20000000, walks program
     * headers, finds PT_DYNAMIC, and reads .dynamic at
     * MAIN_PROG_BASE + dyn_phdr->vaddr.
     *
     * We also handle bss (memsz > filesz) by allocating fresh zero
     * pages, same as the existing PT_LOAD loop below. */
    if (main_elf_data_orig) {
        elf64_hdr_t *main_hdr = (elf64_hdr_t*)main_elf_data_orig;
        if (main_elf_size_orig >= sizeof(elf64_hdr_t)) {
            elf64_phdr_t *main_phdr =
                (elf64_phdr_t*)(main_elf_data_orig + main_hdr->phoff);
            for (int i = 0; i < main_hdr->phnum; i++) {
                if (main_phdr[i].type != 1 /* PT_LOAD */) continue;
                u64 load_vaddr = MAIN_PROG_BASE + main_phdr[i].vaddr;
                if (map_user_pages(proc->as, load_vaddr,
                                   main_elf_data_orig + main_phdr[i].offset,
                                   main_phdr[i].filesz) != 0) {
                    oc_console_puts("dynlink: failed to map main PT_LOAD\n");
                    proc->alive = 0;
                    return -1;
                }
                /* Allocate fresh zero pages for the bss region
                 * (memsz > filesz) so .bss reads as zero. */
                if (main_phdr[i].memsz > main_phdr[i].filesz) {
                    u64 bss_start = load_vaddr + main_phdr[i].filesz;
                    u64 bss_end = bss_start +
                                  (main_phdr[i].memsz - main_phdr[i].filesz);
                    u64 page = bss_start & ~0xFFFULL;
                    while (page < bss_end) {
                        u64 phys;
                        if (!vmm_is_mapped(proc->as, page, &phys)) {
                            phys = pmm_alloc_frame();
                            if (phys) {
                                vmm_map_page(proc->as, page, phys,
                                             VMM_FLAG_PRESENT | VMM_FLAG_WRITE |
                                             VMM_FLAG_USER);
                                oc_memset((void*)phys, 0, PMM_PAGE_SIZE);
                            }
                        }
                        page += PMM_PAGE_SIZE;
                    }
                }
            }
        }

        /* WP-08b Batch 4a: map the embedded libfoo.so (from solib_data.h)
         * to SOLIB_LIBFOO_BASE so ld.so can read its ELF header + .dynamic
         * + .dynsym + .dynstr from a fixed address.
         *
         * Same approach as the main program: libfoo.so is a PIE (ET_DYN),
         * so its p_vaddr values are relative to load base 0; we offset by
         * SOLIB_LIBFOO_BASE so the in-memory image lives at
         * 0x30000000 + p_vaddr.
         *
         * For Batch 4a the kernel maps libfoo.so unconditionally (it's
         * the only embedded .so for now). ld.so will just read the ELF
         * header and validate it; actual .so usage is Batch 4b+. */
        {
            elf64_hdr_t *lib_hdr = (elf64_hdr_t*)solib_libfoo;
            if (solib_libfoo_size >= sizeof(elf64_hdr_t)) {
                elf64_phdr_t *lib_phdr =
                    (elf64_phdr_t*)(solib_libfoo + lib_hdr->phoff);
                for (int i = 0; i < lib_hdr->phnum; i++) {
                    if (lib_phdr[i].type != 1 /* PT_LOAD */) continue;
                    u64 load_vaddr = SOLIB_LIBFOO_BASE + lib_phdr[i].vaddr;
                    if (map_user_pages(proc->as, load_vaddr,
                                       solib_libfoo + lib_phdr[i].offset,
                                       lib_phdr[i].filesz) != 0) {
                        oc_console_puts("dynlink: failed to map libfoo.so PT_LOAD\n");
                        proc->alive = 0;
                        return -1;
                    }
                    /* Allocate fresh zero pages for libfoo.so's bss. */
                    if (lib_phdr[i].memsz > lib_phdr[i].filesz) {
                        u64 bss_start = load_vaddr + lib_phdr[i].filesz;
                        u64 bss_end = bss_start +
                                      (lib_phdr[i].memsz - lib_phdr[i].filesz);
                        u64 page = bss_start & ~0xFFFULL;
                        while (page < bss_end) {
                            u64 phys;
                            if (!vmm_is_mapped(proc->as, page, &phys)) {
                                phys = pmm_alloc_frame();
                                if (phys) {
                                    vmm_map_page(proc->as, page, phys,
                                                 VMM_FLAG_PRESENT | VMM_FLAG_WRITE |
                                                 VMM_FLAG_USER);
                                    oc_memset((void*)phys, 0, PMM_PAGE_SIZE);
                                }
                            }
                            page += PMM_PAGE_SIZE;
                        }
                    }
                }
            }
        }

        /* WP-08b Batch 5: map a writable page at LDSO_API_TABLE_ADDR
         * for ld.so to write its dlopen/dlsym/dlclose function pointers.
         * ld.so fills this at startup; user programs read from 0x08000000
         * to call the dynamic linker API. */
        {
            u64 api_phys = pmm_alloc_frame();
            if (api_phys) {
                vmm_map_page(proc->as, LDSO_API_TABLE_ADDR, api_phys,
                             VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
                oc_memset((void*)api_phys, 0, PMM_PAGE_SIZE);
            }
        }
    }

    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);

    /* P3-13 FIX: ELF program-header bounds validation.
     *
     * Old code did ZERO validation on the program-header table or
     * individual program-header fields before dereferencing them.
     * A malformed (or malicious) ELF could specify:
     *   - phoff pointing past end of file → OOB read of kernel memory
     *   - phentsize != sizeof(elf64_phdr_t) → misaligned iteration
     *   - p_offset + p_filesz > elf_size → OOB read during map_user_pages
     *   - p_vaddr + p_memsz overflow → wraps to small VA, clobbering kernel
     *   - p_memsz < p_filesz → negative BSS size (underflow in loop)
     *   - p_vaddr in kernel space (>= USER_LIMIT) → kernel-page overwrite
     * Each of these is a real, exploitable bug. The kernel MUST validate
     * before trusting any field from a user-supplied binary.
     *
     * The fix: before the load loop, verify:
     *   1. phoff + phnum * phentsize <= elf_size (program-header table fits)
     *   2. phentsize == sizeof(elf64_phdr_t) (no misalignment)
     *   3. For each PT_LOAD: p_offset+p_filesz <= elf_size,
     *      p_vaddr+p_memsz doesn't overflow, p_memsz >= p_filesz,
     *      p_vaddr is in user space (< USER_LIMIT).
     */
    if (hdr->phentsize != sizeof(elf64_phdr_t)) {
        oc_console_puts("[user] bad phentsize\n");
        proc->alive = 0; return -1;
    }
    /* Check phoff + phnum * phentsize <= elf_size (with overflow guard). */
    {
        u64 phdr_table_end = (u64)hdr->phoff + (u64)hdr->phnum * (u64)hdr->phentsize;
        if (hdr->phoff >= elf_size || hdr->phnum == 0 ||
            phdr_table_end < hdr->phoff ||  /* overflow */
            phdr_table_end > elf_size) {
            oc_console_puts("[user] program-header table out of bounds\n");
            proc->alive = 0; return -1;
        }
    }
    /* P3-14 FIX: Pre-validate every PT_LOAD range and check for overlaps.
     * Overlapping PT_LOAD segments would cause the second one to silently
     * overwrite the first one's mappings (and the backing physical pages
     * would be leaked, because map_user_pages allocates a fresh frame and
     * overwrites the PTE without freeing the old frame). */
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != 1 /* PT_LOAD */) continue;
        /* p_offset + p_filesz must be within the ELF file. */
        u64 file_end = phdr[i].offset + phdr[i].filesz;
        if (file_end < phdr[i].offset || file_end > elf_size) {
            oc_console_puts("[user] PT_LOAD file range out of bounds\n");
            proc->alive = 0; return -1;
        }
        /* p_vaddr + p_memsz must not overflow. */
        u64 mem_end = phdr[i].vaddr + phdr[i].memsz;
        if (mem_end < phdr[i].vaddr) {
            oc_console_puts("[user] PT_LOAD vaddr+memsz overflow\n");
            proc->alive = 0; return -1;
        }
        /* p_memsz must be >= p_filesz (BSS section is memsz - filesz). */
        if (phdr[i].memsz < phdr[i].filesz) {
            oc_console_puts("[user] PT_LOAD memsz < filesz\n");
            proc->alive = 0; return -1;
        }
        /* p_vaddr must be in user space. */
        if (phdr[i].vaddr >= 0x0000800000000000ULL) {
            oc_console_puts("[user] PT_LOAD vaddr in kernel space\n");
            proc->alive = 0; return -1;
        }
        /* Check for overlap with previous PT_LOAD segments. */
        for (int j = 0; j < i; j++) {
            if (phdr[j].type != 1) continue;
            u64 j_start = phdr[j].vaddr;
            u64 j_end = phdr[j].vaddr + phdr[j].memsz;
            u64 i_start = phdr[i].vaddr;
            u64 i_end = phdr[i].vaddr + phdr[i].memsz;
            /* Overlap if i_start < j_end && j_start < i_end. */
            if (i_start < j_end && j_start < i_end) {
                oc_console_puts("[user] PT_LOAD segments overlap\n");
                proc->alive = 0; return -1;
            }
        }
    }

    /* P3-14 FIX (additional): before mapping each PT_LOAD, if a page is
     * already mapped (from a previous PT_LOAD — should NOT happen now
     * because we rejected overlaps above, but be defensive), free the
     * old physical frame first. The old map_user_pages path allocated
     * a fresh frame and overwrote the PTE — leaking the old frame.
     * We do this per-page in the loop below. */
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != 1) continue;
        /* For each page in this PT_LOAD's range, if already mapped,
         * free the old physical frame BEFORE map_user_pages allocates
         * a new one and overwrites the PTE. This prevents the leak. */
        {
            u64 v = phdr[i].vaddr & ~0xFFFULL;
            u64 end = phdr[i].vaddr + phdr[i].filesz;
            while (v < end) {
                u64 old_phys;
                if (vmm_is_mapped(proc->as, v, &old_phys)) {
                    /* Unmap + free the old frame. */
                    u64 old_pte = vmm_unmap_page(proc->as, v);
                    if (old_pte & VMM_FLAG_PRESENT) {
                        u64 p = old_pte & 0x000FFFFFFFFFF000ULL;
                        if (p != 0) pmm_free_frame(p);
                    }
                }
                v += 0x1000;
            }
        }
        if (map_user_pages(proc->as, phdr[i].vaddr,
                           elf_data + phdr[i].offset, phdr[i].filesz) != 0) {
            proc->alive = 0; return -1;
        }
        if (phdr[i].memsz > phdr[i].filesz) {
            u64 bss_start = phdr[i].vaddr + phdr[i].filesz;
            u64 bss_end = bss_start + (phdr[i].memsz - phdr[i].filesz);
            u64 page = bss_start & ~0xFFFULL;
            while (page < bss_end) {
                u64 phys;
                if (vmm_is_mapped(proc->as, page, &phys)) {
                    /* BSS FIX: Page is mapped (from split PT in
                     * create_user_address_space) but might lack the
                     * USER flag. Ensure it's user-accessible + writable
                     * + zeroed in the bss portion. */
                    vmm_protect_page(proc->as, page,
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
                    /* Zero the bss portion within this page.
                     * The page is identity-mapped (phys == page for
                     * the split PT range), so we can zero directly. */
                    u64 zero_start = (page < bss_start) ? bss_start : page;
                    u64 zero_end = (page + PMM_PAGE_SIZE < bss_end)
                                   ? page + PMM_PAGE_SIZE : bss_end;
                    if (zero_end > zero_start) {
                        oc_memset((void*)(uintptr_t)zero_start, 0,
                                  (int)(zero_end - zero_start));
                    }
                } else {
                    /* Page not mapped: allocate fresh zero page. */
                    phys = pmm_alloc_frame();
                    if (phys) {
                        vmm_map_page(proc->as, page, phys,
                                     VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
                        oc_memset((void*)phys, 0, PMM_PAGE_SIZE);
                    }
                }
                page += PMM_PAGE_SIZE;
            }
        }
    }

    proc->entry_point = hdr->entry;
    proc->brk = USER_BRK_BASE;

    u64 stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (u64 vaddr = stack_base; vaddr < USER_STACK_TOP; vaddr += PMM_PAGE_SIZE) {
        u64 phys = pmm_alloc_frame();
        if (phys) vmm_map_page(proc->as, vaddr, phys,
                               VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    }
    proc->user_rsp = USER_STACK_TOP - 16;

    proc->tid = kthread_create(user_task_launcher, proc, proc->name, TASK_PRIO_DEFAULT);
    if (proc->tid < 0) { proc->alive = 0; return -1; }
    kthread_set_cr3(proc->tid, proc->as);
    /* BUG-029 FIX: Set rsp0 for TSS stack switching on ring-3 IRQ. */
    {
        task_t *t = kthread_get_task(proc->tid);
        if (t) t->rsp0 = (u64)(uintptr_t)t->stack_base + t->stack_size;
    }
    return proc->pid;
}

int user_process_kill(pid_t pid) {
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].pid == pid) {
            g_procs[i].alive = 0;
            /* BUG-008 FIX: Destroy address space when killing a process.
             * Switch to kernel CR3 first if this is the current process. */
            /* P1-9 FIX: The old code had an EMPTY if block here —
             * vmm_destroy_address_space was never called. */
            if (g_procs[i].as) {
                tid_t cur = kthread_current_tid();
                if (cur == g_procs[i].tid) {
                    __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_as()) : "memory");
                }
                vmm_destroy_address_space(g_procs[i].as);
                g_procs[i].as = 0;
            }
            kthread_destroy(g_procs[i].tid);
            return 0;
        }
    }
    return -1;
}

void user_process_list(void) {
    char buf[80]; char n[20];
    oc_console_puts("PID  TID  ENTRY          NAME\n");
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!g_procs[i].alive) continue;
        oc_strcpy(buf, "");
        oc_u64_to_str((u64)g_procs[i].pid, n); oc_strcpy(buf+oc_strlen(buf), n);
        while (oc_strlen(buf) < 5) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_u64_to_str((u64)g_procs[i].tid, n); oc_strcpy(buf+oc_strlen(buf), n);
        while (oc_strlen(buf) < 10) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_u64_to_hex(g_procs[i].entry_point, n, 8); oc_strcpy(buf+oc_strlen(buf), "0x");
        oc_strcpy(buf+oc_strlen(buf), n);
        while (oc_strlen(buf) < 25) oc_strcpy(buf+oc_strlen(buf), " ");
        oc_strcpy(buf+oc_strlen(buf), g_procs[i].name);
        oc_console_puts(buf);
        oc_console_putc('\n');
    }
}
