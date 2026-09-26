/* SPDX-License-Identifier: Apache-2.0 */
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
#include "sched.h"

/* Syscall table. */
#define MAX_SYSCALLS 256
static syscall_handler_fn g_syscalls[MAX_SYSCALLS];

/* User process table. (non-static for syscall.c) */
user_proc_t g_procs[MAX_USER_PROCS];
static int g_next_pid = 1;

/* WP-08a: saved interrupt frame pointer (for fork). */
static u64 *g_current_frame = NULL;

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
    const char *p = (const char*)buf;
    for (u64 i = 0; i < len; i++) oc_console_putc(p[i]);
    return len;
}

u64 syscall_write_and_exit(u64 buf, u64 len, u64 arg3, u64 arg4) {
    (void)arg3; (void)arg4;
    const u64 USER_LIMIT = 0x0000800000000000ULL;
    if (buf < USER_LIMIT && buf + len <= USER_LIMIT && len <= 65536) {
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

    g_current_frame = NULL;
}

void user_set_current_frame(u64 *frame_regs) { g_current_frame = frame_regs; }
u64 *user_get_current_frame(void) { return g_current_frame; }

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

    /* Split the 2MiB huge page at PD0[2] (0x400000, user code region) */
    u64 old_pde = pd0[2];
    u64 base_phys = old_pde & 0x000FFFFFFFE00000ULL;
    u64 pt_phys = pmm_alloc_frame();
    if (pt_phys == 0) {
        pmm_free_frame(pd0_phys); pmm_free_frame(pdpt_phys); pmm_free_frame(pml4_phys);
        return 0;
    }
    u64 *pt = (u64*)pt_phys;
    oc_memset(pt, 0, PMM_PAGE_SIZE);
    for (int i = 0; i < 512; i++) {
        u64 page_phys = base_phys + (u64)i * PMM_PAGE_SIZE;
        pt[i] = page_phys | 0x003 | 0x0800000000000000ULL;
    }
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
        /* Fork child: enter ring 3 at the saved fork-return point.
         * RAX=0 so the child sees fork() returning 0.
         * True POSIX fork semantics — no is_fork_child check in user code. */
        enter_ring3_fork(proc->fork_rip, proc->fork_rsp, proc->fork_rflags,
                         proc->fork_rax, user_as);
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
        "or $0x200, (%%rsp)\n"
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

    int slot = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!g_procs[i].alive) { slot = i; break; }
    }
    if (slot < 0) return -1;

    user_proc_t *proc = &g_procs[slot];
    oc_memset(proc, 0, sizeof(*proc));
    proc->pid = g_next_pid++;
    proc->alive = 1;
    if (name) oc_strncpy(proc->name, name, 31);

    proc->as = create_user_address_space();
    if (proc->as == 0) { proc->alive = 0; return -1; }

    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != 1) continue;
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
                if (!vmm_is_mapped(proc->as, page, &phys)) {
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
    return proc->pid;
}

int user_process_kill(pid_t pid) {
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].pid == pid) {
            g_procs[i].alive = 0;
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
