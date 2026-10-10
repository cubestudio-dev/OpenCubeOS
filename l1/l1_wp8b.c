/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-08b
 * File: kernel/ext_wp8b.c
 * Purpose: L1 extension interfaces for dynamic linking. Wraps the
 *          kernel's dynamic linking infrastructure (user_process_create's
 *          ET_DYN path, sys_map_solib, solib name table) so L1 extensions
 *          can load and use shared libraries without directly calling
 *          syscalls or parsing ELF structures.
 *
 * 10 interfaces (items 41-50): elf_load_dynamic, ldso_run, so_load,
 * so_unload, symbol_resolve, dlsym_impl, dlopen_impl, dlclose_impl,
 * reloc_apply, elf_get_needed. */

#include "l1_wp8b.h"
#include "core_usermode.h"
#include "mem_vmm.h"
#include "mem_pmm.h"
#include "screen_console.h"
#include "lib_string.h"

/* solib_libfoo is defined in solib_data.h (included by usermode.c).
 * Use extern here to avoid multiple-definition linker error. */
extern const u8 solib_libfoo[];
extern const u64 solib_libfoo_size;

/* ELF64 structures (local definitions, same layout as usermode.c/ld_so.c). */
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

typedef struct {
    u64 tag;
    u64 val;
} __attribute__((packed)) elf64_dyn_t;

typedef struct {
    u32 st_name;
    u8  st_info;
    u8  st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
} __attribute__((packed)) elf64_sym_t;

typedef struct {
    u64 r_offset;
    u64 r_info;
    i64 r_addend;
} __attribute__((packed)) elf64_rela_t;

/* ELF constants. */
#define ET_DYN       3
#define PT_INTERP    3
#define PT_DYNAMIC   2
#define DT_NULL      0
#define DT_NEEDED    1
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_JMPREL    23
#define DT_PLTRELSZ  2
#define R_X86_64_64         1
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8

/* ld.so base address (fixed). */
#define LDSO_BASE 0x10000000ULL

/* ---- BUG-0257 FIX (A16-8): shared bounded-parsing helpers ----
 * The old symbol scans walked a hardcoded entry count (4096 in
 * symbol_resolve, 64 in reloc_apply) without ever consulting the real
 * table bounds, so a malformed ELF made the kernel read past the loaded
 * image (under the shared user CR3 that is a ring-0 #PF). Every scan
 * below is now bounded by metadata derived from the ELF itself. */

/* Upper bound on any .dynsym walk. Legit libraries are far smaller;
 * the cap only stops a runaway loop, it is NOT the primary bound. */
#define DYNSYM_MAX 4096

/* Derive the in-memory image end of a LOADED ELF (handle = base where
 * the ELF header is mapped) from its own program headers:
 * max(p_vaddr + p_memsz) over PT_LOAD, relative to handle.
 * Returns 0 when the header itself is not sane - callers must not
 * parse anything derived from it in that case. */
static u64 elf_image_end(u64 handle) {
    if (!handle) return 0;
    elf64_hdr_t *eh = (elf64_hdr_t*)handle;
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L'  || eh->ident[3] != 'F') return 0;
    if (eh->ident[4] != 2) return 0;                 /* not ELF64 */
    if (eh->phentsize != sizeof(elf64_phdr_t)) return 0;
    if (eh->phnum == 0 || eh->phnum > 64) return 0;
    if (eh->phoff < sizeof(elf64_hdr_t)) return 0;
    if (eh->phoff > 0x10000ULL) return 0;            /* sane-linker cap */
    if (eh->phoff + (u64)eh->phnum * sizeof(elf64_phdr_t) < eh->phoff) return 0;
    elf64_phdr_t *ph = (elf64_phdr_t*)(handle + eh->phoff);
    u64 end = 0;
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type != 1) continue;               /* PT_LOAD */
        u64 e = ph[i].vaddr + ph[i].memsz;
        if (e < ph[i].vaddr) return 0;               /* overflow: insane */
        if (e > end) end = e;
    }
    if (end == 0) return 0;
    return handle + end;
}

/* Number of .dynsym entries for a loaded image, derived from real
 * metadata: prefer DT_HASH nchain when the caller found it, fall back
 * to the classic .dynsym/.dynstr adjacency. All inputs are runtime
 * VAs already validated to lie inside [handle, img_end). Returns 0
 * when the count cannot be bounded (callers must not scan). */
static u64 dynsym_count(u64 img_end, u64 symtab_rt, u64 strtab_rt, u64 hash_rt) {
    if (hash_rt && hash_rt + 8 <= img_end) {
        u32 nbucket = *(u32*)(uintptr_t)hash_rt;
        u32 nchain  = *(u32*)(uintptr_t)(hash_rt + 4);
        if (nchain > 0 && nchain <= DYNSYM_MAX &&
            (u64)nbucket + nchain <= (img_end - hash_rt - 8) / 4)
            return nchain;
    }
    if (strtab_rt > symtab_rt && strtab_rt <= img_end) {
        u64 bytes = strtab_rt - symtab_rt;
        if (bytes % sizeof(elf64_sym_t)) return 0;
        u64 n = bytes / sizeof(elf64_sym_t);
        if (n == 0 || n > DYNSYM_MAX) return 0;
        /* clamp to what actually fits inside the image */
        if (n > (img_end - symtab_rt) / sizeof(elf64_sym_t))
            n = (img_end - symtab_rt) / sizeof(elf64_sym_t);
        return n;
    }
    return 0;
}

/* Bounded NUL-terminated compare: candidate string lives at
 * str_rt + off inside [str_rt, str_rt + cap); name is a trusted
 * kernel-side NUL string. Never reads past the candidate cap. */
static int str_eq_named(u64 str_rt, u64 cap, u32 off, const char *name) {
    if (off >= cap) return 0;
    const char *s = (const char*)(uintptr_t)(str_rt + off);
    u64 max = cap - off;
    for (u64 i = 0; i < max; i++) {
        if (s[i] != name[i]) return 0;
        if (s[i] == 0) return 1;
    }
    return 0;   /* candidate ran out before its NUL: not bounded -> reject */
}

/* Bounded NUL-terminated compare between two strings that BOTH live in
 * validated buffers (used by reloc_apply where the relocating ELF's
 * .dynstr is caller-supplied and must not be trusted). */
static int str_eq_2(u64 a_rt, u64 a_cap, u32 a_off, u64 b_rt, u64 b_cap, u32 b_off) {
    if (a_off >= a_cap || b_off >= b_cap) return 0;
    const char *a = (const char*)(uintptr_t)(a_rt + a_off);
    const char *b = (const char*)(uintptr_t)(b_rt + b_off);
    u64 amax = a_cap - a_off, bmax = b_cap - b_off;
    for (u64 i = 0; i < amax && i < bmax; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;   /* both ended at the same byte */
    }
    return 0;
}

/* Solib name table for so_load (same as syscall.c's g_solib_table). */
struct solib_entry { const char *name; const u8 *data; u64 size; };
static struct solib_entry g_ext_solib_table[8];  /* runtime-initialized */
static int g_solib_table_inited = 0;

static void solib_table_init(void) {
    if (g_solib_table_inited) return;
    g_ext_solib_table[0].name = "libfoo.so";
    g_ext_solib_table[0].data = solib_libfoo;
    g_ext_solib_table[0].size = solib_libfoo_size;
    g_solib_table_inited = 1;
}

/* ---- Interface 41: elf_load_dynamic ---- */
int elf_load_dynamic(const u8 *elf_data, u64 elf_size, const char *name) {
    /* user_process_create already handles ET_DYN detection, PT_INTERP
     * parsing, ld.so lookup + mapping, main program mapping, libfoo.so
     * mapping, API table page mapping, and jumping to ld.so entry. */
    return user_process_create(elf_data, elf_size, name);
}

/* ---- Interface 42: ldso_run ---- */
u64 ldso_run(void) {
    /* ld.so is always mapped at 0x10000000 by the kernel for dynamic
     * processes. This is informational — the actual ld.so execution
     * (parsing .dynamic, resolving symbols, patching GOT/PLT, jumping
     * to main entry) is handled by ld.so itself in user space. */
    user_proc_t *proc = user_process_current();
    if (!proc) return 0;
    /* Check if this is a dynamic process (has ld.so mapped). */
    u64 phys;
    if (mem_vmm_is_mapped(proc->as, LDSO_BASE, &phys)) {
        return LDSO_BASE;
    }
    return 0;
}

/* ---- BUG-0256 FIX (A16-7): BSS zeroing that also covers mapped pages ----
 * map_user_pages() copies only p_filesz bytes into fresh frames and
 * leaves the rest of the boundary page filled with recycled physical
 * memory. The old loop skipped every page that was already mapped, so
 * the filesz/memsz boundary page kept its dirty tail (and a fully
 * mapped BSS page stayed dirty entirely) - the loaded library saw
 * garbage instead of zeros in its uninitialized globals. Now every page
 * in the BSS range is zeroed from the first BSS byte to the end of the
 * range, mapped or not; only genuinely fresh frames are allocated. */
static void so_bss_zero(mem_vmm_as_t as, u64 bs, u64 be) {
    if (be <= bs) return;
    u64 page = bs & ~0xFFFULL;
    while (page < be) {
        u64 page_end = page + PMM_PAGE_SIZE;
        u64 zstart = (bs > page) ? bs : page;      /* first bss byte here */
        u64 zend   = (be < page_end) ? be : page_end;
        u64 phys;
        if (!mem_vmm_is_mapped(as, page, &phys)) {
            phys = mem_pmm_alloc_frame();
            if (!phys) { page = page_end; continue; }  /* OOM: skip (as before) */
            mem_vmm_map_page(as, page, phys,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
        if (zend > zstart)
            memset((void*)(uintptr_t)(phys + (zstart - page)), 0, zend - zstart);
        page = page_end;
    }
}

/* ---- Interface 43: so_load ---- */
u64 so_load(const char *name, u64 flags) {
    (void)flags;
    if (!name) return 0;
    solib_table_init();
    /* Look up .so by name. */
    const u8 *data = NULL;
    u64 size = 0;
    for (int i = 0; i < 8 && g_ext_solib_table[i].name; i++) {
        if (strcmp(name, g_ext_solib_table[i].name) == 0) {
            data = g_ext_solib_table[i].data;
            size = g_ext_solib_table[i].size;
            break;
        }
    }
    if (!data || size < sizeof(elf64_hdr_t)) return 0;
    /* Validate ELF. */
    elf64_hdr_t *hdr = (elf64_hdr_t*)data;
    if (hdr->ident[0] != 0x7f || hdr->ident[1] != 'E' ||
        hdr->ident[2] != 'L'  || hdr->ident[3] != 'F') return 0;
    if (hdr->ident[4] != 2 || hdr->type != ET_DYN) return 0;
    /* Get current process. */
    user_proc_t *proc = user_process_current();
    if (!proc) return 0;
    u64 base = proc->next_solib_addr;
    /* Map each PT_LOAD segment at base + p_vaddr. */
    elf64_phdr_t *phdr = (elf64_phdr_t*)(data + hdr->phoff);
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != 1) continue; /* PT_LOAD */
        u64 load_vaddr = base + phdr[i].vaddr;
        if (map_user_pages(proc->as, load_vaddr,
                           data + phdr[i].offset, phdr[i].filesz) != 0)
            return 0;
        /* bss zero pages (BUG-0256 FIX: also zeroes the mapped boundary
         * page's tail and any fully mapped bss page). */
        if (phdr[i].memsz > phdr[i].filesz) {
            so_bss_zero(proc->as,
                        load_vaddr + phdr[i].filesz,
                        load_vaddr + phdr[i].memsz);
        }
    }
    /* Advance bump allocator. */
    u64 max_end = 0;
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != 1) continue;
        u64 end = phdr[i].vaddr + phdr[i].memsz;
        if (end > max_end) max_end = end;
    }
    proc->next_solib_addr = (base + max_end + 0xFFF) & ~0xFFFULL;
    return base;
}

/* ---- Interface 44: so_unload ---- */
int so_unload(u64 handle) {
    /* BUG-007 FIX: Actually unmap the .so's PT_LOAD pages and free the
     * physical frames. The old implementation was a no-op that claimed
     * to be "a real implementation" — it was not. Repeated dlopen/dlclose
     * would leak memory (bump allocator only grows, never shrinks). */
    if (!handle) return -1;  /* reject NULL handle */
    user_proc_t *proc = user_process_current();
    if (!proc) return -1;
    elf64_hdr_t *eh = (elf64_hdr_t*)handle;
    /* Validate ELF header. */
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L'  || eh->ident[3] != 'F') return -1;
    if (eh->ident[4] != 2 || eh->type != ET_DYN) return -1;
    /* Walk PT_LOAD segments and unmap each page. */
    elf64_phdr_t *ph = (elf64_phdr_t*)(handle + eh->phoff);
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type != 1) continue;  /* PT_LOAD */
        u64 start = handle + ph[i].vaddr;
        u64 end = start + ph[i].memsz;
        /* Page-align. */
        start &= ~0xFFFULL;
        end = (end + 0xFFF) & ~0xFFFULL;
        for (u64 vaddr = start; vaddr < end; vaddr += PMM_PAGE_SIZE) {
            u64 phys = mem_vmm_unmap_page(proc->as, vaddr);
            if (phys) mem_pmm_free_frame(phys);
        }
    }
    return 0;
}

/* ---- Interface 45: symbol_resolve ---- */
u64 symbol_resolve(u64 handle, const char *name) {
    if (!handle || !name) return 0;
    /* BUG-0257 FIX: derive the real image bounds from the ELF header
     * before parsing anything table-shaped. elf_image_end validates
     * magic/class/phdr sanity and returns 0 for malformed headers. */
    u64 img_end = elf_image_end(handle);
    if (!img_end) return 0;
    elf64_hdr_t *eh = (elf64_hdr_t*)handle;
    /* Find PT_DYNAMIC. */
    elf64_phdr_t *phdr = (elf64_phdr_t*)(handle + eh->phoff);
    elf64_phdr_t *dyn_ph = NULL;
    for (int i = 0; i < eh->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) { dyn_ph = &phdr[i]; break; }
    }
    if (!dyn_ph) return 0;
    /* .dynamic content must lie inside the image. */
    u64 dyn_rt = handle + dyn_ph->vaddr;
    if (dyn_ph->filesz > img_end - dyn_rt) return 0;
    elf64_dyn_t *dyn = (elf64_dyn_t*)dyn_rt;
    /* Find DT_SYMTAB / DT_STRTAB / DT_STRSZ / DT_HASH. */
    u64 symtab_val = 0, strtab_val = 0, strsz_val = 0, hash_val = 0;
    u64 ndyn = dyn_ph->filesz / sizeof(elf64_dyn_t);
    for (u64 i = 0; i < ndyn; i++) {
        if (dyn[i].tag == DT_NULL) break;
        if (dyn[i].tag == DT_SYMTAB) symtab_val = dyn[i].val;
        else if (dyn[i].tag == DT_STRTAB) strtab_val = dyn[i].val;
        else if (dyn[i].tag == 10 /* DT_STRSZ */) strsz_val = dyn[i].val;
        else if (dyn[i].tag == 4 /* DT_HASH */) hash_val = dyn[i].val;
    }
    if (!symtab_val || !strtab_val) return 0;
    u64 symtab_rt = handle + symtab_val;
    u64 strtab_rt = handle + strtab_val;
    u64 hash_rt   = hash_val ? handle + hash_val : 0;
    /* BUG-0257 FIX: every table must sit inside the loaded image. */
    if (symtab_rt < handle + sizeof(elf64_hdr_t) || symtab_rt >= img_end) return 0;
    if (strtab_rt < handle || strtab_rt >= img_end) return 0;
    if (hash_val && (hash_rt < handle || hash_rt >= img_end)) return 0;
    /* Real entry count (BUG-0257 FIX: was a blind 4096-entry walk). */
    u64 n = dynsym_count(img_end, symtab_rt, strtab_rt, hash_rt);
    if (!n) return 0;
    /* String reads are bounded by DT_STRSZ when present, else by the
     * end of the image (never by the terminator, which untrusted data
     * may not contain). */
    u64 strcap = img_end - strtab_rt;
    if (strsz_val) {
        if (strsz_val > strcap) return 0;
        strcap = strsz_val;
    }
    /* Iterate .dynsym, find symbol by name.
     * Don't break on null entry at index 0 — it's the standard ELF
     * undefined entry, not a terminator. Just skip st_name==0 entries. */
    elf64_sym_t *sym = (elf64_sym_t*)symtab_rt;
    for (u64 i = 0; i < n; i++) {
        if (sym[i].st_name == 0) continue;
        /* Bounded compare (BUG-0257 FIX: st_name validated inside the
         * string table; the scan can no longer run past the image). */
        if (!str_eq_named(strtab_rt, strcap, sym[i].st_name, name)) continue;
        if (sym[i].st_shndx == 0) continue; /* SHN_UNDEF = undefined */
        if (sym[i].st_value >= img_end - handle) continue; /* not in image */
        return handle + sym[i].st_value;
    }
    return 0;
}

/* ---- Interface 46: dlsym_impl ---- */
u64 dlsym_impl(u64 handle, const char *name) {
    return symbol_resolve(handle, name);
}

/* ---- Interface 47: dlopen_impl ---- */
u64 dlopen_impl(const char *name, u64 flags) {
    return so_load(name, flags);
}

/* ---- Interface 48: dlclose_impl ---- */
int dlclose_impl(u64 handle) {
    return so_unload(handle);
}

/* ---- Interface 49: reloc_apply ---- */
void reloc_apply(const void *rela_data, u64 count, u64 base,
                 u64 symtab_addr, u64 strtab_addr,
                 u64 libfoo_base, u64 libfoo_symtab, u64 libfoo_strtab) {
    elf64_rela_t *rela = (elf64_rela_t*)rela_data;
    /* BUG-0257 FIX: bound every symbol-table walk.
     * Both images (the ELF being relocated, at base, and libfoo, at
     * libfoo_base) are validated from their own headers; the symbol
     * scans below only touch [table, table + n*entsize) inside the
     * derived image, and string reads are bounded by the image end.
     * A count/cap of 0 means "cannot be bounded" -> symbol lookups for
     * that side are skipped (sym_value stays 0), never scanned blind. */
    u64 self_end = base ? elf_image_end(base) : 0;
    u64 foo_end  = libfoo_base ? elf_image_end(libfoo_base) : 0;
    u64 self_n = 0, self_strcap = 0;
    if (self_end && symtab_addr && strtab_addr &&
        symtab_addr >= base + sizeof(elf64_hdr_t) && symtab_addr < self_end &&
        strtab_addr >= base && strtab_addr < self_end) {
        self_n = dynsym_count(self_end, symtab_addr, strtab_addr, 0);
        self_strcap = self_end - strtab_addr;
    }
    u64 foo_n = 0, foo_strcap = 0;
    if (foo_end && libfoo_symtab && libfoo_strtab &&
        libfoo_symtab >= libfoo_base + sizeof(elf64_hdr_t) &&
        libfoo_symtab < foo_end &&
        libfoo_strtab >= libfoo_base && libfoo_strtab < foo_end) {
        foo_n = dynsym_count(foo_end, libfoo_symtab, libfoo_strtab, 0);
        foo_strcap = foo_end - libfoo_strtab;
    }
    elf64_sym_t *symtab = (elf64_sym_t*)symtab_addr;
    elf64_sym_t *libfoo_sym = (elf64_sym_t*)libfoo_symtab;
    for (u64 i = 0; i < count; i++) {
        u64 rtype = rela[i].r_info & 0xffffffff;
        u64 sym_idx = rela[i].r_info >> 32;
        u64 target = base + rela[i].r_offset;
        i64 addend = rela[i].r_addend;
        switch (rtype) {
        case R_X86_64_RELATIVE:
            *(u64*)target = base + addend;
            break;
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_64: {
            /* Look up symbol by name in libfoo.so .dynsym. */
            u64 sym_value = 0;
            /* BUG-0257 FIX: sym_idx was unbounded here (a hostile rela
             * entry read symtab_addr + sym_idx*24 far outside the
             * image -> ring-0 #PF). Only a bounded index in a validated
             * table is dereferenced. */
            if (sym_idx > 0 && sym_idx < self_n && foo_n > 0) {
                /* Get symbol name from the relocating ELF's .dynsym. */
                u32 name_off = symtab[sym_idx].st_name;
                /* Search libfoo.so .dynsym for the same name. */
                for (u64 j = 0; j < foo_n; j++) {
                    if (libfoo_sym[j].st_name == 0) continue;
                    if (!str_eq_2(strtab_addr, self_strcap, name_off,
                                  libfoo_strtab, foo_strcap,
                                  libfoo_sym[j].st_name)) continue;
                    if (libfoo_sym[j].st_shndx != 0 &&         /* defined */
                        libfoo_sym[j].st_value < foo_end - libfoo_base) {
                        sym_value = libfoo_base + libfoo_sym[j].st_value;
                    }
                    break;
                }
            }
            *(u64*)target = sym_value + addend;
            break;
        }
        case R_X86_64_COPY: {
            /* Copy symbol value from libfoo.so to target. */
            if (sym_idx > 0 && sym_idx < self_n && foo_n > 0) {
                u32 name_off = symtab[sym_idx].st_name;
                for (u64 j = 0; j < foo_n; j++) {
                    if (libfoo_sym[j].st_name == 0) continue;
                    if (!str_eq_2(strtab_addr, self_strcap, name_off,
                                  libfoo_strtab, foo_strcap,
                                  libfoo_sym[j].st_name)) continue;
                    if (libfoo_sym[j].st_shndx != 0 &&         /* defined */
                        libfoo_sym[j].st_size > 0 &&
                        libfoo_sym[j].st_value < foo_end - libfoo_base &&
                        libfoo_sym[j].st_size <= foo_end - libfoo_base - libfoo_sym[j].st_value) {
                        u64 src = libfoo_base + libfoo_sym[j].st_value;
                        memcpy((void*)target, (void*)(uintptr_t)src,
                               libfoo_sym[j].st_size);
                    }
                    break;
                }
            }
            break;
        }
        default:
            /* Unknown relocation type — skip. */
            break;
        }
    }
}

/* ---- Interface 50: elf_get_needed ---- */
/* BUG-0258 FIX (A16-9): the signature now carries the buffer size.
 * The old prototype had no way for a caller to pass the boundary, so
 * e_phoff/e_phnum/dyn_offset were read unchecked and a truncated ELF
 * walked straight off the end of the buffer. DT_STRTAB's value is a
 * LINK-TIME vaddr: it is now converted to a real file offset through
 * the PT_LOAD that covers it (the old code used it as a file offset,
 * which only works when vaddr happens to equal offset). Any boundary
 * violation makes the function return 0 - malformed input is rejected,
 * never partially parsed. */
int elf_get_needed(const u8 *elf_data, u64 elf_size,
                   const char *needed_out[], int max_count) {
    if (!elf_data || max_count <= 0) return 0;
    if (elf_size < sizeof(elf64_hdr_t)) return 0;
    /* Validate ELF magic. */
    if (elf_data[0] != 0x7f || elf_data[1] != 'E' ||
        elf_data[2] != 'L'  || elf_data[3] != 'F') return 0;
    if (elf_data[4] != 2) return 0; /* not ELF64 */
    /* Check e_type. */
    u16 e_type = elf_data[16] | (elf_data[17] << 8);
    if (e_type != ET_DYN) return 0; /* not dynamic */
    /* Read phoff + phentsize + phnum, all bounds-checked. */
    u64 e_phoff = 0;
    for (int b = 0; b < 8; b++) e_phoff |= ((u64)elf_data[32 + b]) << (b * 8);
    u16 e_phentsize = elf_data[54] | (elf_data[55] << 8);
    u16 e_phnum = elf_data[56] | (elf_data[57] << 8);
    if (e_phnum == 0 || e_phnum > 64) return 0;
    if (e_phentsize != sizeof(elf64_phdr_t)) return 0;
    if (e_phoff > elf_size ||
        e_phoff + (u64)e_phnum * sizeof(elf64_phdr_t) > elf_size) return 0;
    /* Find PT_DYNAMIC. */
    u64 dyn_offset = 0, dyn_filesz = 0;
    int have_dyn = 0;
    for (int i = 0; i < e_phnum; i++) {
        const u8 *ph = elf_data + e_phoff + (u64)i * sizeof(elf64_phdr_t);
        u32 p_type = ph[0] | (ph[1] << 8) | (ph[2] << 16) | (ph[3] << 24);
        if (p_type == PT_DYNAMIC) {
            for (int b = 0; b < 8; b++) dyn_offset |= ((u64)ph[8 + b]) << (b * 8);
            for (int b = 0; b < 8; b++) dyn_filesz |= ((u64)ph[32 + b]) << (b * 8);
            have_dyn = 1;
            break;
        }
    }
    if (!have_dyn || dyn_filesz == 0) return 0;
    if (dyn_offset > elf_size || dyn_filesz > elf_size - dyn_offset) return 0;
    /* Find DT_STRTAB (+ DT_STRSZ when present, for a tight string cap). */
    u64 strtab_val = 0, strsz_val = 0;
    u64 num_dyn = dyn_filesz / sizeof(elf64_dyn_t);
    for (u64 j = 0; j < num_dyn; j++) {
        const u8 *de = elf_data + dyn_offset + j * sizeof(elf64_dyn_t);
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == DT_NULL) break;
        if (tag == DT_STRTAB) { strtab_val = val; break; }
    }
    for (u64 j = 0; j < num_dyn; j++) {
        const u8 *de = elf_data + dyn_offset + j * sizeof(elf64_dyn_t);
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == DT_NULL) break;
        if (tag == 10 /* DT_STRSZ */) { strsz_val = val; break; }
    }
    if (!strtab_val) return 0;
    /* BUG-0258 FIX: convert the DT_STRTAB vaddr to a file offset via
     * the PT_LOAD segment that covers it (strtab is file-backed). */
    u64 strtab_off = 0;
    int converted = 0;
    for (int i = 0; i < e_phnum; i++) {
        const u8 *ph = elf_data + e_phoff + (u64)i * sizeof(elf64_phdr_t);
        u32 p_type = ph[0] | (ph[1] << 8) | (ph[2] << 16) | (ph[3] << 24);
        if (p_type != 1) continue; /* PT_LOAD */
        u64 p_offset = 0, p_vaddr = 0, p_filesz = 0;
        for (int b = 0; b < 8; b++) p_offset |= ((u64)ph[8 + b]) << (b * 8);
        for (int b = 0; b < 8; b++) p_vaddr  |= ((u64)ph[16 + b]) << (b * 8);
        for (int b = 0; b < 8; b++) p_filesz |= ((u64)ph[32 + b]) << (b * 8);
        if (p_vaddr <= strtab_val && strtab_val - p_vaddr < p_filesz) {
            strtab_off = p_offset + (strtab_val - p_vaddr);
            converted = 1;
            break;
        }
    }
    if (!converted) return 0;
    if (strtab_off >= elf_size) return 0;
    u64 strcap = elf_size - strtab_off;
    if (strsz_val) {
        if (strsz_val > strcap) return 0;
        strcap = strsz_val;
    }
    /* Find DT_NEEDED entries (every string NUL-checked inside the cap). */
    int count = 0;
    for (u64 j = 0; j < num_dyn && count < max_count; j++) {
        const u8 *de = elf_data + dyn_offset + j * sizeof(elf64_dyn_t);
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == DT_NULL) break;
        if (tag == DT_NEEDED) {
            if (val >= strcap) return 0;   /* name outside the string table */
            u64 off = strtab_off + val;
            u64 lim = strtab_off + strcap; /* <= elf_size */
            u64 k = off;
            while (k < lim && elf_data[k]) k++;
            if (k >= lim) return 0;        /* not NUL-terminated: reject */
            needed_out[count] = (const char*)(elf_data + off);
            count++;
        }
    }
    return count;
}

/* ---- Self-test ----
 * BUG-0248 FIX (A16-10): the stale "so_unload always succeeds" assertion
 * is corrected (so_unload(0) returns -1 since the BUG-007 fix) and the
 * suite now also carries the BUG-0256/0257/0258 double-sided assertions
 * (legit input PASS + malformed input rejected). The caller is the
 * kernel shell's `l1test` command, so the suite is actually executed. */
static u8 g_t_vaddr_blob[0xE9];    /* dynamic ELF whose vaddr != file offset */
static u8 g_t_wild_blob[0xE0];     /* dynamic ELF with DT_STRTAB wild pointer */
static u8 g_t_badsym_blob[0xE0];   /* dynamic ELF with DT_SYMTAB wild pointer */
static u8 g_t_reloc_img[0x200];    /* relocatable image for reloc_apply */

static void t_put16(u8 *p, u64 off, u16 v) { for (int i = 0; i < 2; i++) p[off+i] = (u8)(v >> (8*i)); }
static void t_put32(u8 *p, u64 off, u32 v) { for (int i = 0; i < 4; i++) p[off+i] = (u8)(v >> (8*i)); }
static void t_put64(u8 *p, u64 off, u64 v) { for (int i = 0; i < 8; i++) p[off+i] = (u8)(v >> (8*i)); }

static void t_build_hdr(u8 *p, u16 phnum) {
    p[0] = 0x7f; p[1] = 'E'; p[2] = 'L'; p[3] = 'F'; p[4] = 2;  /* ELF64 LSB */
    t_put16(p, 16, 3);      /* e_type = ET_DYN */
    t_put16(p, 18, 0x3e);   /* e_machine = EM_X86_64 */
    t_put32(p, 20, 1);      /* e_version */
    t_put64(p, 32, 0x40);   /* e_phoff */
    t_put16(p, 52, 64);     /* e_ehsize */
    t_put16(p, 54, 56);     /* e_phentsize */
    t_put16(p, 56, phnum);
}

static void t_build_ph(u8 *p, u64 off, u32 type, u64 foff, u64 va, u64 filesz, u64 memsz) {
    t_put32(p, off, type);
    t_put32(p, off + 4, 5);          /* PF_R|PF_X */
    t_put64(p, off + 8, foff);
    t_put64(p, off + 16, va);
    t_put64(p, off + 24, 0);         /* p_paddr */
    t_put64(p, off + 32, filesz);
    t_put64(p, off + 40, memsz);
    t_put64(p, off + 48, 0x1000);    /* p_align */
}

static void t_build_fixtures(void) {
    /* g_t_vaddr_blob: strtab lives at file offset 0xE0 but its DT_STRTAB
     * vaddr is 0x10E0 (PT_LOAD maps off 0 -> va 0x1000). A reader that
     * uses the vaddr as a file offset reads past this tiny buffer. */
    t_build_hdr(g_t_vaddr_blob, 2);
    t_build_ph(g_t_vaddr_blob, 0x40, 1 /*PT_LOAD*/, 0, 0x1000, 0xE9, 0xE9);
    t_build_ph(g_t_vaddr_blob, 0x78, 2 /*PT_DYNAMIC*/, 0xB0, 0x10B0, 0x30, 0x30);
    t_put64(g_t_vaddr_blob, 0xB0, 1);  t_put64(g_t_vaddr_blob, 0xB8, 1);        /* DT_NEEDED 1 */
    t_put64(g_t_vaddr_blob, 0xC0, 5);  t_put64(g_t_vaddr_blob, 0xC8, 0x10E0);   /* DT_STRTAB */
    t_put64(g_t_vaddr_blob, 0xD0, 0);  t_put64(g_t_vaddr_blob, 0xD8, 0);        /* DT_NULL */
    g_t_vaddr_blob[0xE0] = 0;
    for (int i = 0; i < 8; i++) g_t_vaddr_blob[0xE1 + i] = "libx.so"[i];        /* + NUL at 0xE8 */
    g_t_vaddr_blob[0xE8] = 0;

    /* g_t_wild_blob: DT_STRTAB points at an unmapped canonical address. */
    t_build_hdr(g_t_wild_blob, 2);
    t_build_ph(g_t_wild_blob, 0x40, 1, 0, 0, 0xE0, 0xE0);
    t_build_ph(g_t_wild_blob, 0x78, 2, 0xB0, 0xB0, 0x30, 0x30);
    t_put64(g_t_wild_blob, 0xB0, 5);  t_put64(g_t_wild_blob, 0xB8, 0x400000000000ULL);
    t_put64(g_t_wild_blob, 0xC0, 0);  t_put64(g_t_wild_blob, 0xC8, 0);

    /* g_t_badsym_blob: DT_SYMTAB/DT_STRTAB point far outside the image. */
    t_build_hdr(g_t_badsym_blob, 2);
    t_build_ph(g_t_badsym_blob, 0x40, 1, 0, 0, 0xE0, 0xE0);
    t_build_ph(g_t_badsym_blob, 0x78, 2, 0xB0, 0xB0, 0x30, 0x30);
    t_put64(g_t_badsym_blob, 0xB0, 6);  t_put64(g_t_badsym_blob, 0xB8, 0x400000000000ULL);
    t_put64(g_t_badsym_blob, 0xC0, 5);  t_put64(g_t_badsym_blob, 0xC8, 0x400000001000ULL);
    t_put64(g_t_badsym_blob, 0xD0, 0);  t_put64(g_t_badsym_blob, 0xD8, 0);

    /* g_t_reloc_img: ELF image with a one-entry .dynsym/.dynstr, a
     * .rela slot and a scratch target area.
     *   0x040 phdr | 0x078 symtab[0..1] | 0x0A8 strtab "\\0foo_add\\0"
     *   0x0B8 rela[0..2] | 0x100/0x108/0x110 targets */
    t_build_hdr(g_t_reloc_img, 1);
    t_build_ph(g_t_reloc_img, 0x40, 1, 0, 0, 0x200, 0x200);
    /* symtab[0]: the SHN_UNDEF null entry; symtab[1]: foo_add. */
    t_put32(g_t_reloc_img, 0x78, 0);
    t_put32(g_t_reloc_img, 0x90, 1);                         /* sym1.st_name = 1 */
    g_t_reloc_img[0x94] = 0x12;                              /* st_info = GLOBAL|FUNC */
    t_put16(g_t_reloc_img, 0x96, 5);                         /* st_shndx = 5 */
    t_put64(g_t_reloc_img, 0x98, 0x1000);                    /* st_value */
    t_put64(g_t_reloc_img, 0xA0, 4);                         /* st_size */
    g_t_reloc_img[0xA8] = 0;
    for (int i = 0; i < 7; i++) g_t_reloc_img[0xA9 + i] = "foo_add"[i];
    g_t_reloc_img[0xB0] = 0;
    /* rela[0]: GLOB_DAT sym 1, addend 0x10 -> resolved from libfoo. */
    t_put64(g_t_reloc_img, 0xB8, 0x100);                     /* r_offset */
    t_put64(g_t_reloc_img, 0xC0, (1ULL << 32) | 6);          /* GLOB_DAT sym 1 */
    t_put64(g_t_reloc_img, 0xC8, 0x10);                      /* r_addend */
    /* rela[1]: RELATIVE -> base + 0x1234 written at base+0x108. */
    t_put64(g_t_reloc_img, 0xD0, 0x108);
    t_put64(g_t_reloc_img, 0xD8, 8);                         /* R_X86_64_RELATIVE */
    t_put64(g_t_reloc_img, 0xE0, 0x1234);
    /* rela[2]: GLOB_DAT with hostile sym_idx -> must be skipped. */
    t_put64(g_t_reloc_img, 0xE8, 0x110);
    t_put64(g_t_reloc_img, 0xF0, (0x10000000ULL << 32) | 6);
    t_put64(g_t_reloc_img, 0xF8, 0);
}

#define G2_WP8B_TEST_COUNT 14

int ext_wp8b_selftest(void) {
    int passed = 0;
    const char *needed[8];
    char num[20];
    extern const u8 userprog_so_test[];
    extern const u64 userprog_so_test_size;
    extern const u8 userprog_hello[];
    extern const u64 userprog_hello_size;
    t_build_fixtures();

    /* Test 1 (legit): elf_get_needed on a dynamic ELF (so_test) with its
     * real size -> exactly one DT_NEEDED: "libfoo.so". */
    int n = elf_get_needed(userprog_so_test, userprog_so_test_size, needed, 8);
    if (n == 1 && needed[0] && strcmp(needed[0], "libfoo.so") == 0) {
        screen_console_puts("  ext_wp8b: elf_get_needed(so_test) = 1 (libfoo.so) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: elf_get_needed(so_test) - FAIL\n");
    }
    /* Test 2 (legit): static ELF (hello) -> 0. */
    n = elf_get_needed(userprog_hello, userprog_hello_size, needed, 8);
    if (n == 0) {
        screen_console_puts("  ext_wp8b: elf_get_needed(hello) = 0 (static, correct) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: elf_get_needed(hello) - FAIL\n");
    }
    /* Test 3 (BUG-0248 FIX): so_unload rejects the NULL handle (-1 since
     * the BUG-007 fix; the old suite asserted == 0 and could never pass). */
    if (so_unload(0) == -1) {
        screen_console_puts("  ext_wp8b: so_unload(0) = -1 (NULL handle rejected) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: so_unload(0) - FAIL\n");
    }
    /* Test 4: ldso_run returns 0 in kernel context (no user process). */
    if (ldso_run() == 0) {
        screen_console_puts("  ext_wp8b: ldso_run = 0 (kernel context, correct) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: ldso_run - FAIL\n");
    }
    /* Test 5 (BUG-0258 hostile): truncated buffer -> rejected by bounds. */
    n = elf_get_needed(userprog_so_test, 48, needed, 8);
    if (n == 0) {
        screen_console_puts("  ext_wp8b: elf_get_needed(truncated 48B) = 0 (rejected) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: elf_get_needed(truncated) - FAIL\n");
    }
    /* Test 6 (BUG-0258 legit): vaddr -> file-offset conversion. */
    n = elf_get_needed(g_t_vaddr_blob, sizeof(g_t_vaddr_blob), needed, 8);
    if (n == 1 && needed[0] && strcmp(needed[0], "libx.so") == 0) {
        screen_console_puts("  ext_wp8b: elf_get_needed(vaddr!=off) = 1 (libx.so) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: elf_get_needed(vaddr!=off) - FAIL\n");
    }
    /* Test 7 (BUG-0258 hostile): wild DT_STRTAB vaddr -> rejected. */
    n = elf_get_needed(g_t_wild_blob, sizeof(g_t_wild_blob), needed, 8);
    if (n == 0) {
        screen_console_puts("  ext_wp8b: elf_get_needed(wild strtab) = 0 (rejected) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: elf_get_needed(wild strtab) - FAIL\n");
    }
    /* Test 8 (BUG-0256): BSS zeroing covers the mapped boundary page.
     * Scratch address space: page0/page1 pre-mapped with dirty 0xA7
     * frames, page2 unmapped. BSS range [va0+1000, va0+1000+9216)
     * spans the boundary page tail, a full mapped page and a fresh one. */
    do {
        u64 va0 = 0x38000000ULL;
        u64 p0 = mem_pmm_alloc_frame();
        u64 p1 = mem_pmm_alloc_frame();
        mem_vmm_as_t as = create_user_address_space();
        int ok = p0 && p1 && as;
        if (ok) {
            memset((void*)(uintptr_t)p0, 0xA7, PMM_PAGE_SIZE);
            memset((void*)(uintptr_t)p1, 0xA7, PMM_PAGE_SIZE);
            mem_vmm_map_page(as, va0, p0,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            mem_vmm_map_page(as, va0 + PMM_PAGE_SIZE, p1,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            so_bss_zero(as, va0 + 1000, va0 + 1000 + 2 * PMM_PAGE_SIZE + 512);
            u8 *b0 = (u8*)(uintptr_t)p0;
            u8 *b1 = (u8*)(uintptr_t)p1;
            u64 p2 = 0;
            ok = b0[0] == 0xA7 && b0[999] == 0xA7 &&      /* file data intact */
                 b0[1000] == 0 && b0[PMM_PAGE_SIZE - 1] == 0 && /* boundary tail zeroed */
                 b1[0] == 0 && b1[PMM_PAGE_SIZE - 1] == 0 &&    /* mapped bss page zeroed */
                 mem_vmm_is_mapped(as, va0 + 2 * PMM_PAGE_SIZE, &p2) &&
                 p2 != 0 && ((u8*)(uintptr_t)p2)[511] == 0;     /* fresh page mapped+zero */
            /* cleanup: unmap + free, destroy the scratch AS. */
            u64 ph;
            if (mem_vmm_is_mapped(as, va0, &ph) && ph) mem_pmm_free_frame(ph);
            if (mem_vmm_is_mapped(as, va0 + PMM_PAGE_SIZE, &ph) && ph) mem_pmm_free_frame(ph);
            if (mem_vmm_is_mapped(as, va0 + 2 * PMM_PAGE_SIZE, &ph) && ph) mem_pmm_free_frame(ph);
            mem_vmm_unmap_page(as, va0);
            mem_vmm_unmap_page(as, va0 + PMM_PAGE_SIZE);
            mem_vmm_unmap_page(as, va0 + 2 * PMM_PAGE_SIZE);
            mem_vmm_destroy_address_space(as);
        } else {
            if (p0) mem_pmm_free_frame(p0);
            if (p1) mem_pmm_free_frame(p1);
        }
        if (ok) {
            screen_console_puts("  ext_wp8b: so_bss_zero boundary page (0xA7 tail -> 0) - PASS\n");
            passed++;
        } else {
            screen_console_puts("  ext_wp8b: so_bss_zero boundary page - FAIL\n");
        }
    } while (0);
    /* Tests 9+10 (BUG-0257 legit): bounded resolve on the embedded
     * libfoo.so image (kernel .data handle) - both exported symbols. */
    u64 foo = (u64)(uintptr_t)solib_libfoo;
    if (symbol_resolve(foo, "foo_add") == foo + 0x1000) {
        screen_console_puts("  ext_wp8b: symbol_resolve(libfoo, foo_add) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: symbol_resolve(libfoo, foo_add) - FAIL\n");
    }
    if (symbol_resolve(foo, "foo_global") == foo + 0x3000) {
        screen_console_puts("  ext_wp8b: symbol_resolve(libfoo, foo_global) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: symbol_resolve(libfoo, foo_global) - FAIL\n");
    }
    /* Test 11 (BUG-0257 hostile): DT_SYMTAB outside the image -> rejected
     * without dereferencing the wild address (old code #PF'd here). */
    if (symbol_resolve((u64)(uintptr_t)g_t_badsym_blob, "foo_add") == 0) {
        screen_console_puts("  ext_wp8b: symbol_resolve(wild symtab) = 0 (rejected) - PASS\n");
        passed++;
    } else {
        screen_console_puts("  ext_wp8b: symbol_resolve(wild symtab) - FAIL\n");
    }
    /* Tests 12-14: reloc_apply. */
    do {
        u64 base = (u64)(uintptr_t)g_t_reloc_img;
        reloc_apply(g_t_reloc_img + 0xB8, 3, base,
                    base + 0x78, base + 0xA8,
                    foo, foo + 0x250, foo + 0x298);
        u64 v_glob = *(u64*)(uintptr_t)(base + 0x100);
        u64 v_rel  = *(u64*)(uintptr_t)(base + 0x108);
        u64 v_bad  = *(u64*)(uintptr_t)(base + 0x110);
        if (v_glob == (u64)solib_libfoo + 0x1000 + 0x10) {
            screen_console_puts("  ext_wp8b: reloc GLOB_DAT cross-lib resolve - PASS\n");
            passed++;
        } else {
            screen_console_puts("  ext_wp8b: reloc GLOB_DAT cross-lib resolve - FAIL\n");
        }
        if (v_rel == base + 0x1234) {
            screen_console_puts("  ext_wp8b: reloc RELATIVE base+addend - PASS\n");
            passed++;
        } else {
            screen_console_puts("  ext_wp8b: reloc RELATIVE base+addend - FAIL\n");
        }
        if (v_bad == 0) {   /* hostile sym_idx skipped, addend-only result */
            screen_console_puts("  ext_wp8b: reloc hostile sym_idx bounded - PASS\n");
            passed++;
        } else {
            screen_console_puts("  ext_wp8b: reloc hostile sym_idx bounded - FAIL\n");
        }
    } while (0);

    screen_console_puts("  ext_wp8b: ");
    u64_to_str((u64)passed, num);
    screen_console_puts(num);
    screen_console_puts("/");
    u64_to_str((u64)G2_WP8B_TEST_COUNT, num);
    screen_console_puts(num);
    screen_console_puts(" self-tests passed\n");
    return passed;
}
