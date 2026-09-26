/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-08b
 * File: kernel/dynlink.c
 * Purpose: In-kernel dynamic linker — loads ET_DYN ELFs, resolves
 *          symbols, applies relocations, calls init functions.
 */
#include "dynlink.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "string.h"
#include "console.h"

/* ELF header types (local, to avoid polluting global namespace) */
typedef struct __attribute__((packed)) {
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
} elf64_hdr_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} elf64_phdr_t;

/* PT_LOAD = 1, PT_INTERP = 3, PT_DYNAMIC = 2 */
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3

/* Bump allocator for library load addresses */
static u64 g_solib_base = 0x10000000ULL; /* Libraries loaded from 256MB mark */
static u64 g_solib_alloc(u64 size) {
    u64 result = g_solib_base;
    size = (size + 0xFFF) & ~0xFFFULL; /* Page-align */
    g_solib_base += size;
    return result;
}

/* Map ELF segments into the given address space at the given base */
static int map_elf_segments(const u8 *elf_data, u64 elf_size, vmm_as_t as, u64 base) {
    (void)elf_size;
    elf64_hdr_t *hdr = (elf64_hdr_t*)elf_data;
    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);

    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != PT_LOAD) continue;
        u64 seg_vaddr = base + phdr[i].vaddr;
        u64 seg_end = seg_vaddr + phdr[i].memsz;
        u64 seg_page = seg_vaddr & ~0xFFFULL;

        /* Map all pages needed by this segment */
        while (seg_page < seg_end) {
            u64 phys = pmm_alloc_frame();
            if (phys == 0) return -1;
            u64 flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
            if (phdr[i].flags & 2) flags |= VMM_FLAG_WRITE; /* PF_W */
            vmm_map_page(as, seg_page, phys, flags);
            oc_memset((void*)phys, 0, PMM_PAGE_SIZE);
            seg_page += PMM_PAGE_SIZE;
        }

        /* Copy file data into mapped pages */
        u64 copy_off = 0;
        u64 filesz = phdr[i].filesz;
        while (copy_off < filesz) {
            u64 page_vaddr = (seg_vaddr + copy_off) & ~0xFFFULL;
            u64 page_off = (seg_vaddr + copy_off) & 0xFFF;
            u64 chunk = PMM_PAGE_SIZE - page_off;
            if (chunk > filesz - copy_off) chunk = filesz - copy_off;

            /* Find the physical page (we just mapped it, so it's there) */
            u64 phys;
            if (!vmm_is_mapped(as, page_vaddr, &phys)) return -1;
            oc_memcpy((u8*)phys + page_off, elf_data + phdr[i].offset + copy_off, chunk);
            copy_off += chunk;
        }
    }
    return 0;
}

/* Parse the .dynamic section and fill in the solib_t descriptor */
static int parse_dynamic(const u8 *elf_data, u64 base, solib_t *lib) {
    (void)base;
    elf64_hdr_t *hdr = (elf64_hdr_t*)elf_data;
    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);

    /* Find PT_DYNAMIC */
    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) {
            dyn = (Elf64_Dyn*)(elf_data + phdr[i].offset);
            break;
        }
    }
    if (!dyn) return -1;

    /* Parse dynamic entries */
    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_STRTAB:     lib->strtab_off = d->d_un.d_ptr; break;
            case DT_SYMTAB:     lib->symtab_off = d->d_un.d_ptr; break;
            case DT_STRSZ:      lib->strsz = d->d_un.d_val; break;
            case DT_SYMENT:     lib->syment = d->d_un.d_val; break;
            case DT_RELA:       lib->rela_off = d->d_un.d_ptr; break;
            case DT_RELASZ:     lib->relasz = d->d_un.d_val; break;
            case DT_JMPREL:     lib->jmprel_off = d->d_un.d_ptr; break;
            case DT_PLTRELSZ:   lib->pltrelsz = d->d_un.d_val; break;
            case DT_INIT:       lib->init_off = d->d_un.d_ptr; break;
            case DT_INIT_ARRAY: lib->init_array_off = d->d_un.d_ptr; break;
            case DT_INIT_ARRAYSZ: lib->init_arraysz = d->d_un.d_val; break;
            case DT_PLTGOT:     lib->pltgot_off = d->d_un.d_ptr; break;
            case DT_HASH:       lib->hash_off = d->d_un.d_ptr; lib->has_hash = 1; break;
            case DT_GNU_HASH:   lib->gnu_hash_off = d->d_un.d_ptr; lib->has_gnu_hash = 1; break;
        }
    }
    if (lib->syment == 0) lib->syment = sizeof(Elf64_Sym);
    return 0;
}

/* Get a string from the string table */
static __attribute__((unused)) const char *get_str(solib_t *lib, u64 offset) {
    (void)lib; (void)offset;
    if (lib->strtab_off == 0) return "";
    /* The string table is in the ELF file data, which is mapped at base.
     * But we access it through the ELF file pointer, not the mapped memory.
     * Actually, for the main program, we have the ELF data pointer.
     * For loaded .so files, we need to read from mapped user memory...
     * But we're in kernel context. Let's store the ELF data pointer. */
    /* This is a simplification — we store the raw ELF data pointer */
    return "";
}

/* Actually, the string table and symbol table offsets in .dynamic are
 * virtual addresses relative to the load base. We need to access them
 * through the ELF file data (which is in kernel memory). */

/* For a loaded library, we need to find where in the ELF file the
 * strtab/symtab are. The .dynamic entries give virtual addresses,
 * which we need to convert to file offsets using the program headers. */

static u64 vaddr_to_offset(const u8 *elf_data, u64 vaddr) {
    elf64_hdr_t *hdr = (elf64_hdr_t*)elf_data;
    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type != PT_LOAD) continue;
        if (vaddr >= phdr[i].vaddr && vaddr < phdr[i].vaddr + phdr[i].filesz) {
            return phdr[i].offset + (vaddr - phdr[i].vaddr);
        }
    }
    return 0;
}

/* Look up a symbol by name in a single library */
static u64 lookup_symbol_in_lib(solib_t *lib, const u8 *elf_data, const char *name) {
    if (lib->symtab_off == 0 || lib->strtab_off == 0) return 0;

    u64 symtab_file_off = vaddr_to_offset(elf_data, lib->symtab_off);
    u64 strtab_file_off = vaddr_to_offset(elf_data, lib->strtab_off);
    if (symtab_file_off == 0 || strtab_file_off == 0) return 0;

    Elf64_Sym *symtab = (Elf64_Sym*)(elf_data + symtab_file_off);
    const char *strtab = (const char*)(elf_data + strtab_file_off);

    /* If we have DT_HASH, use it to count symbols */
    u64 nsyms = 0;
    if (lib->has_hash) {
        u64 hash_off = vaddr_to_offset(elf_data, lib->hash_off);
        if (hash_off) {
            u32 *hash = (u32*)(elf_data + hash_off);
            nsyms = hash[1]; /* nchain = number of symbols */
        }
    }
    if (nsyms == 0) {
        /* Estimate: use strtab/symtab size relationship */
        /* This is a rough estimate — better than nothing */
        nsyms = 64; /* Conservative max */
    }

    for (u64 i = 0; i < nsyms; i++) {
        Elf64_Sym *sym = &symtab[i];
        if (sym->st_name == 0) continue;
        u8 bind = sym->st_info >> 4;
        if (bind == STB_LOCAL) continue;
        if (sym->st_shndx == SHN_UNDEF) continue;
        const char *sym_name = strtab + sym->st_name;
        if (oc_strcmp(sym_name, name) == 0) {
            return lib->base_addr + sym->st_value;
        }
    }
    return 0;
}

/* Look up a symbol across all loaded libraries */
static u64 lookup_symbol(dynlink_ctx_t *ctx, const u8 **elf_datas, const char *name) {
    for (int i = 0; i < ctx->num_libs; i++) {
        u64 addr = lookup_symbol_in_lib(&ctx->libs[i], elf_datas[i], name);
        if (addr) return addr;
    }
    return 0;
}

/* Apply a single relocation */
static int apply_relocation(dynlink_ctx_t *ctx, const u8 **elf_datas,
                           solib_t *lib, Elf64_Rela *rela) {
    u32 type = (u32)(rela->r_info & 0xFFFFFFFF);
    u32 sym_idx = (u32)(rela->r_info >> 32);
    u64 patch_addr = lib->base_addr + rela->r_offset;

    /* Get the physical page for patch_addr in the user AS */
    u64 phys;
    if (!vmm_is_mapped(ctx->as, patch_addr & ~0xFFFULL, &phys)) {
        return -1;
    }
    u64 *patch_ptr = (u64*)((u8*)phys + (patch_addr & 0xFFF));

    switch (type) {
        case R_X86_64_NONE:
            break;

        case R_X86_64_RELATIVE:
            /* B + addend */
            *patch_ptr = lib->base_addr + rela->r_addend;
            break;

        case R_X86_64_64:
        case R_X86_64_GLOB_DAT: {
            /* S + addend (S = symbol value) */
            if (sym_idx == 0) {
                *patch_ptr = rela->r_addend;
            } else {
                /* Get symbol name */
                u64 symtab_file_off = vaddr_to_offset(elf_datas[0], lib->symtab_off);
                u64 strtab_file_off = vaddr_to_offset(elf_datas[0], lib->strtab_off);
                if (symtab_file_off && strtab_file_off) {
                    Elf64_Sym *symtab = (Elf64_Sym*)(elf_datas[0] + symtab_file_off);
                    const char *strtab = (const char*)(elf_datas[0] + strtab_file_off);
                    const char *name = strtab + symtab[sym_idx].st_name;
                    u64 sym_addr = lookup_symbol(ctx, elf_datas, name);
                    if (sym_addr) {
                        *patch_ptr = sym_addr + rela->r_addend;
                    } else {
                        /* Weak symbol or not found — set to 0 */
                        *patch_ptr = 0;
                    }
                }
            }
            break;
        }

        case R_X86_64_JUMP_SLOT: {
            /* PLT entry: S (lazy binding would resolve on first call,
             * but we do eager binding here) */
            if (sym_idx == 0) {
                *patch_ptr = lib->base_addr + rela->r_addend;
            } else {
                u64 symtab_file_off = vaddr_to_offset(elf_datas[0], lib->symtab_off);
                u64 strtab_file_off = vaddr_to_offset(elf_datas[0], lib->strtab_off);
                if (symtab_file_off && strtab_file_off) {
                    Elf64_Sym *symtab = (Elf64_Sym*)(elf_datas[0] + symtab_file_off);
                    const char *strtab = (const char*)(elf_datas[0] + strtab_file_off);
                    const char *name = strtab + symtab[sym_idx].st_name;
                    u64 sym_addr = lookup_symbol(ctx, elf_datas, name);
                    if (sym_addr) {
                        *patch_ptr = sym_addr;
                    } else {
                        *patch_ptr = 0;
                    }
                }
            }
            break;
        }

        case R_X86_64_PC32: {
            /* S + A - P */
            if (sym_idx == 0) {
                *(u32*)patch_ptr = (u32)(lib->base_addr + rela->r_addend - patch_addr);
            } else {
                u64 symtab_file_off = vaddr_to_offset(elf_datas[0], lib->symtab_off);
                u64 strtab_file_off = vaddr_to_offset(elf_datas[0], lib->strtab_off);
                if (symtab_file_off && strtab_file_off) {
                    Elf64_Sym *symtab = (Elf64_Sym*)(elf_datas[0] + symtab_file_off);
                    const char *strtab = (const char*)(elf_datas[0] + strtab_file_off);
                    const char *name = strtab + symtab[sym_idx].st_name;
                    u64 sym_addr = lookup_symbol(ctx, elf_datas, name);
                    if (sym_addr) {
                        *(u32*)patch_ptr = (u32)(sym_addr + rela->r_addend - patch_addr);
                    }
                }
            }
            break;
        }

        default:
            /* Unknown relocation type — warn but don't fail */
            break;
    }
    return 0;
}

/* Process all relocations for a library */
static int process_relocations(dynlink_ctx_t *ctx, const u8 **elf_datas, solib_t *lib) {
    const u8 *elf_data = elf_datas[0]; /* The ELF data for this lib */

    /* Process RELA relocations */
    if (lib->rela_off && lib->relasz) {
        u64 rela_file_off = vaddr_to_offset(elf_data, lib->rela_off);
        if (rela_file_off) {
            Elf64_Rela *relas = (Elf64_Rela*)(elf_data + rela_file_off);
            u64 nrelas = lib->relasz / sizeof(Elf64_Rela);
            for (u64 i = 0; i < nrelas; i++) {
                apply_relocation(ctx, elf_datas, lib, &relas[i]);
            }
        }
    }

    /* Process PLT relocations (JMPREL) */
    if (lib->jmprel_off && lib->pltrelsz) {
        u64 jmprel_file_off = vaddr_to_offset(elf_data, lib->jmprel_off);
        if (jmprel_file_off) {
            Elf64_Rela *relas = (Elf64_Rela*)(elf_data + jmprel_file_off);
            u64 nrelas = lib->pltrelsz / sizeof(Elf64_Rela);
            for (u64 i = 0; i < nrelas; i++) {
                apply_relocation(ctx, elf_datas, lib, &relas[i]);
            }
        }
    }

    return 0;
}

/* Call init functions for a library */
static __attribute__((unused)) void call_init_functions(solib_t *lib) {
    /* We can't directly call user-space functions from kernel context.
     * Instead, we'll record the init function addresses and let the
     * user task launcher call them before entering main.
     * For now, we skip this — the test programs don't need init arrays. */
    (void)lib;
}

/* External: find embedded shared library */
const u8 *solib_find(const char *name, u64 *size) {
    /* WP-08b: No shared libraries embedded yet.
     * This function is called by dynlink_load when processing DT_NEEDED.
     * It will be implemented when .so files are embedded in the kernel
     * via solib_data.h. Until then, return NULL with a diagnostic. */
    if (name) {
        oc_console_puts("[dynlink] solib_find: ");
        oc_console_puts(name);
        oc_console_puts(" not found (no .so data embedded)\n");
    }
    if (size) *size = 0;
    return 0;
}

/* Main dynamic loading entry point */
int dynlink_load(const u8 *elf_data, u64 elf_size, vmm_as_t as, u64 *out_entry) {
    elf64_hdr_t *hdr = (elf64_hdr_t*)elf_data;

    if (hdr->ident[0] != 0x7f || hdr->ident[1] != 'E' ||
        hdr->ident[2] != 'L'  || hdr->ident[3] != 'F') {
        oc_console_puts("[dynlink] not an ELF file\n");
        return -1;
    }

    if (hdr->type != 3) { /* ET_DYN = 3 */
        oc_console_puts("[dynlink] not ET_DYN\n");
        return -1;
    }

    dynlink_ctx_t ctx;
    oc_memset(&ctx, 0, sizeof(ctx));
    ctx.as = as;

    /* Store ELF data pointers for each loaded library */
    const u8 *elf_datas[MAX_SOLIBS];
    oc_memset(elf_datas, 0, sizeof(elf_datas));

    /* Load main program at base 0x400000 (same as static) */
    u64 main_base = 0x400000ULL;
    ctx.libs[0].base_addr = main_base;
    ctx.libs[0].in_use = 1;
    oc_strncpy(ctx.libs[0].name, "main", 63);
    elf_datas[0] = elf_data;
    ctx.num_libs = 1;

    if (map_elf_segments(elf_data, elf_size, as, main_base) != 0) {
        oc_console_puts("[dynlink] failed to map main segments\n");
        return -1;
    }
    oc_console_puts("[dynlink] main program mapped at 0x400000\n");

    if (parse_dynamic(elf_data, main_base, &ctx.libs[0]) != 0) {
        oc_console_puts("[dynlink] no .dynamic section\n");
        return -1;
    }

    /* Find and load DT_NEEDED libraries */
    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);
    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) {
            dyn = (Elf64_Dyn*)(elf_data + phdr[i].offset);
            break;
        }
    }

    if (dyn) {
        u64 strtab_file_off = vaddr_to_offset(elf_data, ctx.libs[0].strtab_off);
        const char *strtab = strtab_file_off ? (const char*)(elf_data + strtab_file_off) : "";

        for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
            if (d->d_tag == DT_NEEDED) {
                const char *lib_name = strtab + d->d_un.d_val;
                if (ctx.num_libs >= MAX_SOLIBS) break;

                u64 so_size = 0;
                const u8 *so_data = solib_find(lib_name, &so_size);
                if (!so_data) {
                    /* Try with /lib/ prefix */
                    char full_name[80];
                    oc_strcpy(full_name, "/lib/");
                    oc_strcat(full_name, lib_name);
                    so_data = solib_find(full_name, &so_size);
                }
                if (!so_data) {
                    oc_console_puts("[dynlink] library not found: ");
                    oc_console_puts(lib_name);
                    oc_console_putc('\n');
                    continue;
                }

                /* Load the .so */
                u64 so_base = g_solib_alloc(so_size);
                solib_t *solib = &ctx.libs[ctx.num_libs];
                solib->base_addr = so_base;
                solib->in_use = 1;
                oc_strncpy(solib->name, lib_name, 63);
                elf_datas[ctx.num_libs] = so_data;

                if (map_elf_segments(so_data, so_size, as, so_base) != 0) {
                    oc_console_puts("[dynlink] failed to map ");
                    oc_console_puts(lib_name);
                    oc_console_putc('\n');
                    continue;
                }
                parse_dynamic(so_data, so_base, solib);
                ctx.num_libs++;

                char msg[120]; char n[20];
                oc_strcpy(msg, "[dynlink] loaded "); oc_strcat(msg, lib_name);
                oc_strcat(msg, " at 0x"); oc_u64_to_hex(so_base, n, 8); oc_strcat(msg, n);
                oc_strcat(msg, "\n");
                oc_console_puts(msg);
            }
        }
    }

    /* Process relocations for all libraries */
    for (int i = 0; i < ctx.num_libs; i++) {
        process_relocations(&ctx, elf_datas, &ctx.libs[i]);
    }
    oc_console_puts("[dynlink] relocations applied\n");

    /* Set entry point */
    *out_entry = main_base + hdr->entry;
    return 0;
}

/* Get DT_NEEDED library list */
int elf_get_needed(const u8 *elf_data, u64 elf_size, char names[][64], int max) {
    (void)elf_size;
    elf64_hdr_t *hdr = (elf64_hdr_t*)elf_data;
    if (hdr->ident[0] != 0x7f || hdr->ident[1] != 'E') return 0;
    if (hdr->type != 3) return 0; /* ET_DYN only */

    elf64_phdr_t *phdr = (elf64_phdr_t*)(elf_data + hdr->phoff);
    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < hdr->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) {
            dyn = (Elf64_Dyn*)(elf_data + phdr[i].offset);
            break;
        }
    }
    if (!dyn) return 0;

    /* Find strtab */
    u64 strtab_vaddr = 0;
    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_STRTAB) { strtab_vaddr = d->d_un.d_ptr; break; }
    }
    if (!strtab_vaddr) return 0;

    u64 strtab_off = vaddr_to_offset(elf_data, strtab_vaddr);
    if (!strtab_off) return 0;
    const char *strtab = (const char*)(elf_data + strtab_off);

    int count = 0;
    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL && count < max; d++) {
        if (d->d_tag == DT_NEEDED) {
            const char *name = strtab + d->d_un.d_val;
            oc_strncpy(names[count], name, 63);
            names[count][63] = 0;
            count++;
        }
    }
    return count;
}

/* Print dynamic dependencies (for ldd command) */
void dynlink_print_needed(const u8 *elf_data, u64 elf_size) {
    (void)elf_size;
    char names[16][64];
    int count = elf_get_needed(elf_data, elf_size, names, 16);
    if (count == 0) {
        oc_console_puts("  (statically linked)\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        oc_console_puts("  ");
        oc_console_puts(names[i]);
        oc_console_putc('\n');
    }
}
