/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-08b
 * File: kernel/dynlink.h
 * Purpose: Dynamic linking support — ELF dynamic segment parsing,
 *          shared library loading, symbol resolution, relocations.
 *
 * The kernel itself acts as the dynamic linker (no external ld.so needed).
 * When loading an ET_DYN ELF, the kernel:
 *   1. Maps the ELF segments at a chosen base address
 *   2. Parses the .dynamic section
 *   3. Loads DT_NEEDED shared libraries (embedded in kernel)
 *   4. Resolves symbols and applies relocations
 *   5. Calls DT_INIT / DT_INIT_ARRAY functions
 *   6. Enters ring 3 at the program entry point
 */
#ifndef OC_DYNLINK_H
#define OC_DYNLINK_H

#include "types.h"
#include "vmm.h"

/* Maximum shared libraries per process */
#define MAX_SOLIBS 8

/* Maximum symbol resolution depth */
#define MAX_SYMBOL_LOOKUPS 256

/* ELF dynamic tags (subset) */
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_GNU_HASH     0x6ffffef5
#define DT_RELACOUNT    0x6ffffff9
#define DT_RELCOUNT     0x6ffffffa
#define DT_FLAGS_1      0x6ffffffb

/* x86_64 relocation types */
#define R_X86_64_NONE       0
#define R_X86_64_64         1
#define R_X86_64_PC32       2
#define R_X86_64_GOT32      3
#define R_X86_64_PLT32      4
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8
#define R_X86_64_GOTPCREL   9
#define R_X86_64_32         10
#define R_X86_64_32S        11
#define R_X86_64_16         12
#define R_X86_64_PC16       13
#define R_X86_64_8          14
#define R_X86_64_PC8        15
#define R_X86_64_PC64       24

/* ELF symbol binding */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2

/* ELF symbol type */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

/* ELF symbol visibility */
#define SHN_UNDEF   0

/* Relocation entry (x86_64 uses RELA) */
typedef struct __attribute__((packed)) {
    u64 r_offset;   /* Address to patch (relative to load base) */
    u64 r_info;     /* Symbol index << 32 | type */
    i64 r_addend;   /* Addend */
} Elf64_Rela;

/* Dynamic section entry */
typedef struct __attribute__((packed)) {
    i64 d_tag;      /* Tag */
    union {
        u64 d_val;  /* Value */
        u64 d_ptr;  /* Pointer */
    } d_un;
} Elf64_Dyn;

/* Symbol table entry */
typedef struct __attribute__((packed)) {
    u32 st_name;    /* String table index */
    u8  st_info;    /* Binding + type */
    u8  st_other;   /* Visibility */
    u16 st_shndx;   /* Section index */
    u64 st_value;   /* Symbol value (relative to load base) */
    u64 st_size;    /* Size */
} Elf64_Sym;

/* Loaded shared library descriptor */
typedef struct {
    int in_use;
    char name[64];          /* Library name (e.g. "libfoo.so") */
    u64 base_addr;          /* Load base address in user AS */
    u64 entry_point;        /* Entry point (for main program) */
    u64 *dynamic;           /* Pointer to .dynamic section (in kernel AS, mapped) */

    /* Dynamic section info */
    u64 strtab_off;         /* DT_STRTAB offset from base */
    u64 symtab_off;         /* DT_SYMTAB offset from base */
    u64 strsz;              /* DT_STRSZ */
    u64 syment;             /* DT_SYMENT */
    u64 rela_off;           /* DT_RELA offset from base */
    u64 relasz;             /* DT_RELASZ */
    u64 jmprel_off;         /* DT_JMPREL offset from base */
    u64 pltrelsz;           /* DT_PLTRELSZ */
    u64 init_off;           /* DT_INIT offset from base */
    u64 init_array_off;     /* DT_INIT_ARRAY offset from base */
    u64 init_arraysz;       /* DT_INIT_ARRAYSZ */
    u64 pltgot_off;         /* DT_PLTGOT offset from base */
    u64 hash_off;           /* DT_HASH offset from base */
    u64 gnu_hash_off;       /* DT_GNU_HASH offset from base */
    int has_hash;
    int has_gnu_hash;
} solib_t;

/* Process dynamic linking state */
typedef struct {
    solib_t libs[MAX_SOLIBS];   /* Loaded libraries (libs[0] = main program) */
    int num_libs;
    vmm_as_t as;                /* Address space for mapping */
} dynlink_ctx_t;

/* ---- Public API ---- */

/* Load and link a dynamic ELF (ET_DYN).
 * elf_data: pointer to the ELF file data
 * elf_size: size of the ELF data
 * as: address space to map into
 * out_entry: receives the entry point address
 * Returns 0 on success, -1 on failure */
int dynlink_load(const u8 *elf_data, u64 elf_size, vmm_as_t as, u64 *out_entry);

/* Find a shared library by name (embedded in kernel).
 * Returns pointer to ELF data, or NULL if not found.
 * Sets *size to the ELF data size. */
const u8 *solib_find(const char *name, u64 *size);

/* Print the dynamic dependencies of an ELF file.
 * Used by the `ldd` command. */
void dynlink_print_needed(const u8 *elf_data, u64 elf_size);

/* Get the list of DT_NEEDED libraries for an ELF.
 * Fills names[] with library names, returns count. */
int elf_get_needed(const u8 *elf_data, u64 elf_size, char names[][64], int max);

#endif /* OC_DYNLINK_H */
