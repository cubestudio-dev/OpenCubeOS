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

#include "ext_wp8b.h"
#include "usermode.h"
#include "vmm.h"
#include "pmm.h"
#include "console.h"
#include "string.h"

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
    if (vmm_is_mapped(proc->as, LDSO_BASE, &phys)) {
        return LDSO_BASE;
    }
    return 0;
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
        if (oc_strcmp(name, g_ext_solib_table[i].name) == 0) {
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
        /* bss zero pages. */
        if (phdr[i].memsz > phdr[i].filesz) {
            u64 bs = load_vaddr + phdr[i].filesz;
            u64 be = bs + (phdr[i].memsz - phdr[i].filesz);
            u64 page = bs & ~0xFFFULL;
            while (page < be) {
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
            u64 phys = vmm_unmap_page(proc->as, vaddr);
            if (phys) pmm_free_frame(phys);
        }
    }
    return 0;
}

/* ---- Interface 45: symbol_resolve ---- */
u64 symbol_resolve(u64 handle, const char *name) {
    if (!handle || !name) return 0;
    elf64_hdr_t *eh = (elf64_hdr_t*)handle;
    /* Validate ELF header. */
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L'  || eh->ident[3] != 'F') return 0;
    if (eh->ident[4] != 2) return 0;
    /* Find PT_DYNAMIC. */
    elf64_phdr_t *phdr = (elf64_phdr_t*)(handle + eh->phoff);
    elf64_phdr_t *dyn_ph = NULL;
    for (int i = 0; i < eh->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) { dyn_ph = &phdr[i]; break; }
    }
    if (!dyn_ph) return 0;
    elf64_dyn_t *dyn = (elf64_dyn_t*)(handle + dyn_ph->vaddr);
    /* Find DT_SYMTAB and DT_STRTAB. */
    u64 symtab = 0, strtab = 0;
    for (int i = 0; ; i++) {
        if (dyn[i].tag == DT_NULL) break;
        if (dyn[i].tag == DT_SYMTAB) symtab = handle + dyn[i].val;
        else if (dyn[i].tag == DT_STRTAB) strtab = handle + dyn[i].val;
    }
    if (!symtab || !strtab) return 0;
    /* Iterate .dynsym, find symbol by name.
     * BUG-044 FIX: Iterate up to 4096 entries (was hardcoded 64).
     * Don't break on null entry at index 0 — it's the standard ELF
     * undefined entry, not a terminator. Just skip st_name==0 entries. */
    elf64_sym_t *sym = (elf64_sym_t*)symtab;
    for (int i = 0; i < 4096; i++) {
        if (sym[i].st_name == 0) continue;
        const char *s = (const char*)(strtab + sym[i].st_name);
        /* Compare name byte by byte. */
        int k = 0;
        while (s[k] && s[k] == name[k]) k++;
        if (s[k] == '\0' && name[k] == '\0') {
            /* Found! Return base + st_value. */
            if (sym[i].st_shndx == 0) continue; /* SHN_UNDEF = undefined */
            return handle + sym[i].st_value;
        }
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
    elf64_sym_t *symtab = (elf64_sym_t*)symtab_addr;
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
            if (sym_idx > 0 && libfoo_symtab && libfoo_strtab) {
                /* Get symbol name from the relocating ELF's .dynsym. */
                const char *name = (const char*)(strtab_addr + symtab[sym_idx].st_name);
                /* Search libfoo.so .dynsym for the same name. */
                elf64_sym_t *libfoo_sym = (elf64_sym_t*)libfoo_symtab;
                for (int j = 0; j < 64; j++) {
                    if (libfoo_sym[j].st_name == 0) continue;
                    const char *s = (const char*)(libfoo_strtab + libfoo_sym[j].st_name);
                    int k = 0;
                    while (s[k] && s[k] == name[k]) k++;
                    if (s[k] == '\0' && name[k] == '\0') {
                        if (libfoo_sym[j].st_shndx != 0) { /* defined */
                            sym_value = libfoo_base + libfoo_sym[j].st_value;
                        }
                        break;
                    }
                }
            }
            *(u64*)target = sym_value + addend;
            break;
        }
        case R_X86_64_COPY: {
            /* Copy symbol value from libfoo.so to target. */
            if (sym_idx > 0 && libfoo_symtab && libfoo_strtab) {
                const char *name = (const char*)(strtab_addr + symtab[sym_idx].st_name);
                elf64_sym_t *libfoo_sym = (elf64_sym_t*)libfoo_symtab;
                for (int j = 0; j < 64; j++) {
                    if (libfoo_sym[j].st_name == 0) continue;
                    const char *s = (const char*)(libfoo_strtab + libfoo_sym[j].st_name);
                    int k = 0;
                    while (s[k] && s[k] == name[k]) k++;
                    if (s[k] == '\0' && name[k] == '\0') {
                        if (libfoo_sym[j].st_shndx != 0 && libfoo_sym[j].st_size > 0) {
                            u64 src = libfoo_base + libfoo_sym[j].st_value;
                            oc_memcpy((void*)target, (void*)src, libfoo_sym[j].st_size);
                        }
                        break;
                    }
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
int elf_get_needed(const u8 *elf_data, const char *needed_out[], int max_count) {
    if (!elf_data || max_count <= 0) return 0;
    /* Validate ELF magic. */
    if (elf_data[0] != 0x7f || elf_data[1] != 'E' ||
        elf_data[2] != 'L'  || elf_data[3] != 'F') return 0;
    if (elf_data[4] != 2) return 0; /* not ELF64 */
    /* Check e_type. */
    u16 e_type = elf_data[16] | (elf_data[17] << 8);
    if (e_type != ET_DYN) return 0; /* not dynamic */
    /* Read phoff + phnum. */
    u64 e_phoff = 0;
    for (int b = 0; b < 8; b++) e_phoff |= ((u64)elf_data[32 + b]) << (b * 8);
    u16 e_phnum = elf_data[56] | (elf_data[57] << 8);
    /* Find PT_DYNAMIC. */
    u64 dyn_offset = 0, dyn_filesz = 0;
    for (int i = 0; i < e_phnum; i++) {
        const u8 *ph = elf_data + e_phoff + (u64)i * 56;
        u32 p_type = ph[0] | (ph[1] << 8) | (ph[2] << 16) | (ph[3] << 24);
        if (p_type == PT_DYNAMIC) {
            for (int b = 0; b < 8; b++) dyn_offset |= ((u64)ph[8 + b]) << (b * 8);
            for (int b = 0; b < 8; b++) dyn_filesz |= ((u64)ph[32 + b]) << (b * 8);
            break;
        }
    }
    if (dyn_filesz == 0) return 0;
    /* Find DT_STRTAB. */
    u64 strtab_val = 0;
    u64 num_dyn = dyn_filesz / 16;
    for (u64 j = 0; j < num_dyn; j++) {
        const u8 *de = elf_data + dyn_offset + j * 16;
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == DT_NULL) break;
        if (tag == DT_STRTAB) { strtab_val = val; break; }
    }
    if (!strtab_val) return 0;
    /* Find DT_NEEDED entries. */
    int count = 0;
    for (u64 j = 0; j < num_dyn && count < max_count; j++) {
        const u8 *de = elf_data + dyn_offset + j * 16;
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == DT_NULL) break;
        if (tag == DT_NEEDED) {
            needed_out[count] = (const char*)(elf_data + strtab_val + val);
            count++;
        }
    }
    return count;
}

/* ---- Self-test ---- */
int ext_wp8b_selftest(void) {
    int passed = 0;
    /* Test 1: elf_get_needed on a dynamic ELF (so_test). */
    extern const u8 userprog_so_test[];
    const char *needed[8];
    int n = elf_get_needed(userprog_so_test, needed, 8);
    if (n > 0) {
        oc_console_puts("  ext_wp8b: elf_get_needed found ");
        char buf[20]; oc_u64_to_str((u64)n, buf);
        oc_console_puts(buf);
        oc_console_puts(" dependency\n");
        passed++;
    }
    /* Test 2: elf_get_needed on a static ELF (hello) → 0. */
    extern const u8 userprog_hello[];
    n = elf_get_needed(userprog_hello, needed, 8);
    if (n == 0) {
        oc_console_puts("  ext_wp8b: elf_get_needed(hello) = 0 (static, correct)\n");
        passed++;
    }
    /* Test 3: so_unload always succeeds. */
    if (so_unload(0) == 0) {
        oc_console_puts("  ext_wp8b: so_unload = 0 (success)\n");
        passed++;
    }
    /* Test 4: ldso_run returns 0 for non-dynamic context. */
    /* (In kernel context, user_process_current() may be NULL,
     * so ldso_run returns 0. This is correct behavior.) */
    if (ldso_run() == 0) {
        oc_console_puts("  ext_wp8b: ldso_run = 0 (kernel context, correct)\n");
        passed++;
    }
    oc_console_puts("  ext_wp8b: ");
    char buf2[20]; oc_u64_to_str((u64)passed, buf2);
    oc_console_puts(buf2);
    oc_console_puts("/4 self-tests passed\n");
    return passed;
}
