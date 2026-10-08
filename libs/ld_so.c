/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */

/* Open Cube OS WP-08b Batch 5 - ld.so generalized dynamic linker.
 *
 * The kernel maps (BUG-0216 FIX: the main/libfoo/dlopen bases are
 * randomized per process WITHIN the regions below; only the API table
 * page keeps a fixed address):
 *   - ld.so (this program) at 0x10000000 (single PT_LOAD)
 *   - main program ELF (PIE bytes) in 0x20000000..0x2FF00000
 *   - libfoo.so (PIE bytes) in 0x30000000..0x37F00000
 *   - API table page at 0x08000000 (R+W, 1 page, FIXED):
 *       offset  0/ 8/16 : dlopen/dlsym/dlclose function pointers
 *       offset 24/32    : main / libfoo.so load bases (written by the
 *                         kernel, read by ld.so — the kernel owns the
 *                         randomization, ld.so owns no fixed base)
 *   - dlopen'd .so files: kernel bump allocator from 0x38000000
 *     (start offset randomized per process)
 *
 * _start flow:
 *   1. Read main/libfoo bases from the API page, sanity-check them
 *   2. Validate main ELF (magic/ELF64/ET_DYN/bounded program headers)
 *   3. Register main + libfoo in the loaded-module table
 *   4. Finish libfoo: load its DT_NEEDED dependencies (recursive,
 *      depth-bounded at 8, via SYS_MAP_SOLIB), relocate it against ALL
 *      loaded modules, then apply W^X to its PT_LOAD segments
 *   5. Finish main the same way (its DT_NEEDED entries are loaded and
 *      relocated before main's own relocations are processed)
 *   6. Install API table at 0x08000000 (dlopen/dlsym/dlclose ptrs)
 *   7. Build a SysV initial process stack ([rsp]=argc, argv[]+NULL,
 *      envp[]+NULL, auxv with AT_PAGESZ/AT_NULL), push the return
 *      trampoline (issues sys_exit2 with main's return value), switch
 *      rsp and enter the main program's entry (BUG-0214 FIX)
 *
 * The generalized process_relocations() handles R_X86_64_RELATIVE,
 * R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT, R_X86_64_COPY.
 * Symbol lookup goes through ALL loaded modules' .dynsym in load order
 * (first loaded wins); undefined-weak symbols resolve to 0 + r_addend;
 * undefined STRONG symbols abort the load with a message (BUG-0213 FIX).
 */

/* Syscall numbers (per kernel/syscall.h) */
#define SYS_WRITE      1
#define SYS_EXIT2    16
#define SYS_MPROTECT  32   /* addr, len, prot; prot bit0=R bit1=W bit2=X */
#define SYS_MAP_SOLIB 90   /* name_ptr, name_len, flags -> base or 0 */

/* Inline syscall: int 0x80, rax=number, rdi/rsi/rdx/r10=args. */
static inline long sys_write(int fd, const void *buf, long len) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((long)SYS_WRITE), "D"((long)fd), "S"(buf), "d"(len)
        : "memory", "rcx", "r11"
    );
    return ret;
}

static inline void sys_exit2(int code) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"((long)SYS_EXIT2), "D"((long)code)
        : "memory", "rcx", "r11"
    );
    for (;;) { }  /* should not return */
}

/* BUG-0216 FIX (W^X): after a module's relocations are applied, drop +W
 * from its executable PT_LOADs (and drop +X from its data PT_LOADs —
 * the kernel's mprotect sets the NX bit when PROT_EXEC is absent, and
 * EFER.NXE is enabled by the kernel). prot uses POSIX bits: R=1 W=2 X=4. */
static inline long sys_mprotect(unsigned long addr, unsigned long len,
                                long prot) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((long)SYS_MPROTECT), "D"((long)addr), "S"((long)len), "d"(prot)
        : "memory", "rcx", "r11"
    );
    return ret;
}

/* Map a shared object by name through the kernel's embedded solib
 * table. The kernel accepts ONLY names it has embedded (today exactly
 * one: "libfoo.so"), validates the ELF itself, maps every PT_LOAD at a
 * per-process bump base and returns that base (0 on failure). */
static inline long sys_map_solib(const char *name, long len, long flags) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((long)SYS_MAP_SOLIB), "D"(name), "S"((long)len), "d"((long)flags)
        : "memory", "rcx", "r11"
    );
    return ret;
}

/* Freestanding string helpers (no libc). */
static long strlen_(const char *s) {
    long n = 0;
    while (s[n]) n++;
    return n;
}

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static void puts_str(const char *s) {
    sys_write(1, s, strlen_(s));
}

/* Print v as 16-char lowercase hex with leading zeros. */
static void puts_hex(unsigned long v) {
    char buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        int d = v & 0xf;
        buf[i] = d < 10 ? ('0' + d) : ('a' + d - 10);
        v >>= 4;
    }
    buf[16] = '\0';
    sys_write(1, buf, 16);
}

/* ELF64 structures (packed, matching the kernel's definitions). */
typedef struct {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    unsigned long entry;
    unsigned long phoff;
    unsigned long shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} __attribute__((packed)) elf64_hdr;

typedef struct {
    unsigned int type;
    unsigned int flags;
    unsigned long offset;
    unsigned long vaddr;
    unsigned long paddr;
    unsigned long filesz;
    unsigned long memsz;
    unsigned long align;
} __attribute__((packed)) elf64_phdr;

typedef struct {
    long tag;
    unsigned long val;   /* d_val or d_ptr (union in spec, but same size) */
} __attribute__((packed)) elf64_dyn;

/* ELF64 symbol table entry (24 bytes). */
typedef struct {
    unsigned int  st_name;     /* offset into .dynstr of the symbol name */
    unsigned char st_info;     /* type + binding (e.g., FUNC + GLOBAL) */
    unsigned char st_other;    /* visibility */
    unsigned short st_shndx;  /* section index (or SHN_UNDEF=0 for undef) */
    unsigned long st_value;   /* symbol value (vaddr for defined symbols) */
    unsigned long st_size;    /* size of the symbol (e.g., function size) */
} __attribute__((packed)) elf64_sym;

/* ELF64 relocation entry with addend (24 bytes). */
typedef struct {
    unsigned long r_offset;   /* vaddr where to apply the relocation */
    unsigned long r_info;     /* high 32 bits = symbol index, low 32 bits = reloc type */
    long r_addend;            /* addend */
} __attribute__((packed)) elf64_rela;

/* Constants from the ELF spec. */
#define ET_DYN       3
#define PT_LOAD      1
#define PT_DYNAMIC   2

#define DT_NULL      0
#define DT_NEEDED    1
#define DT_PLTRELSZ  2
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_STRSZ     10
#define DT_JMPREL    23      /* address of .rela.plt */

/* x86_64 relocation types */
#define R_X86_64_64        1   /* word + addend (direct) */
#define R_X86_64_COPY      5
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

/* Symbol binding (st_info >> 4). */
#define STB_GLOBAL 1
#define STB_WEAK   2

/* SHN_UNDEF: symbol is undefined (declared but not defined in this module). */
#define SHN_UNDEF 0

/* Program-header p_flags. */
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* POSIX mprotect prot bits (matches sys_mem_mprotect's ABI). */
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

/* SysV auxv tags for the initial process stack. */
#define AT_NULL    0
#define AT_PAGESZ  6

/* Load-base regions (BUG-0216 FIX): NOT fixed load addresses. The
 * kernel randomizes each actual base within its region and hands it to
 * us via two reserved slots in the API page; ld.so only sanity-checks
 * those values against the regions. LDSO_API_TABLE_ADDR itself stays
 * FIXED — user programs read dlopen/dlsym/dlclose through it. */
#define LDSO_API_TABLE_ADDR 0x08000000UL
#define LDSO_API_MAIN_BASE_SLOT   24  /* byte offset: main program base  */
#define LDSO_API_LIBFOO_BASE_SLOT 32  /* byte offset: libfoo.so base     */
#define MAIN_REGION_BASE   0x20000000UL
#define MAIN_REGION_END    0x30000000UL
#define LIB_REGION_BASE    0x30000000UL
#define LIB_REGION_END     0x38000000UL

/* Loaded-module table (BUG-0213 FIX).
 *
 * Every module ld.so knows about is registered here: the main program
 * and the kernel-pre-mapped libfoo.so seed the table at startup;
 * DT_NEEDED dependencies and dlopen() calls extend it. Symbol lookup
 * walks ALL registered modules' .dynsym in load order. */
#define LDSO_MAX_MODULES   16
#define LDSO_NAME_MAX      32
#define LDSO_DYN_MAX       64      /* .dynamic walk bound (BUG-0065 style) */
#define LDSO_ELF_PHNUM_MAX 256     /* program-header count sanity bound (BUG-0215) */
#define LDSO_ELF_PHOFF_MAX 0x100000UL  /* phdr table must live in the first MiB */
#define LDSO_SYM_COUNT_MAX 65536   /* hard cap on the derived .dynsym count */
#define LDSO_MAX_DEPTH     8       /* DT_NEEDED recursion bound */

#define LDSO_MOD_EMPTY   0        /* table slot free */
#define LDSO_MOD_LOADING 1        /* registered, deps+relocs pending */
#define LDSO_MOD_READY   2        /* deps loaded, relocated, W^X applied */

struct ldso_module {
    char          name[LDSO_NAME_MAX];
    unsigned long base;      /* load base (randomized, kernel-provided) */
    elf64_dyn    *dyn;       /* PT_DYNAMIC (0 if the module has none)   */
    unsigned long strtab;    /* absolute .dynstr address                */
    unsigned long strsz;     /* DT_STRSZ                                */
    unsigned long symtab;    /* absolute .dynsym address                */
    unsigned long rela;      /* absolute .rela.dyn address              */
    unsigned long relasz;    /* DT_RELASZ bytes                         */
    unsigned long jmprel;    /* absolute .rela.plt address              */
    unsigned long pltrelsz;  /* DT_PLTRELSZ bytes                       */
    unsigned long symcount;  /* derived from the DT_STRTAB-DT_SYMTAB
                               * distance / 24, capped (BUG-0213 FIX:
                               * replaces the blind 4096-entry scan)     */
    elf64_phdr   *phdrs;     /* mapped program-header table             */
    unsigned long phnum;
    unsigned char state;     /* LDSO_MOD_*                              */
};

/* All of this is zero-initialized: it lands in .bss, which ld_so.ld
 * merges into ld.so's single PT_LOAD — the script needs no changes. */
static struct ldso_module g_modules[LDSO_MAX_MODULES];

/* ELF-header sanity (BUG-0215 FIX): magic, ELFCLASS64, ET_DYN and a
 * BOUNDED program-header table. Used consistently by dlopen, dlsym's
 * fallback path, module registration (main + libfoo + every later
 * module) — the main-load path no longer walks phnum unbounded. */
static int elf64_sane(const elf64_hdr *eh) {
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L'  || eh->ident[3] != 'F') {
        return 0;
    }
    if (eh->ident[4] != 2) return 0;  /* ELF64 */
    if (eh->type != ET_DYN) return 0;
    if (eh->phnum == 0 || eh->phnum > LDSO_ELF_PHNUM_MAX) return 0;
    if (eh->phentsize != sizeof(elf64_phdr)) return 0;
    if (eh->phoff < sizeof(elf64_hdr)) return 0;
    if (eh->phoff > LDSO_ELF_PHOFF_MAX) return 0;
    if (eh->phoff + (unsigned long)eh->phnum * sizeof(elf64_phdr)
        > LDSO_ELF_PHOFF_MAX) {
        return 0;
    }
    return 1;
}

/* Find PT_DYNAMIC in an ELF's program headers. Caller must have passed
 * the header through elf64_sane() first (phnum is bounded there).
 * Returns 0 on failure. */
static elf64_dyn *find_dynamic(const elf64_hdr *ehdr, unsigned long base) {
    elf64_phdr *phdr = (elf64_phdr *)(base + ehdr->phoff);
    int i;
    for (i = 0; i < (int)ehdr->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) {
            return (elf64_dyn *)(base + phdr[i].vaddr);
        }
    }
    return 0;
}

/* One bounded pass over a .dynamic array, collecting the tags ld.so
 * needs. Walk is bounded by LDSO_DYN_MAX entries (a crafted .dynamic
 * without DT_NULL cannot make it run away). */
static void dyn_scan(const elf64_dyn *dyn, unsigned long base,
                     unsigned long *strtab, unsigned long *strsz,
                     unsigned long *symtab,
                     unsigned long *rela, unsigned long *relasz,
                     unsigned long *jmprel, unsigned long *pltrelsz) {
    int i;
    for (i = 0; i < LDSO_DYN_MAX; i++) {
        if (dyn[i].tag == DT_NULL) break;
        switch (dyn[i].tag) {
            case DT_STRTAB:   *strtab   = base + dyn[i].val; break;
            case DT_STRSZ:    *strsz    = dyn[i].val;        break;
            case DT_SYMTAB:   *symtab   = base + dyn[i].val; break;
            case DT_RELA:     *rela     = base + dyn[i].val; break;
            case DT_RELASZ:   *relasz   = dyn[i].val;        break;
            case DT_JMPREL:   *jmprel   = base + dyn[i].val; break;
            case DT_PLTRELSZ: *pltrelsz = dyn[i].val;        break;
            default: break;
        }
    }
}

/* BUG-0213 FIX: per-module symbol count derived from the DT_STRTAB -
 * DT_SYMTAB distance (24 bytes per Elf64_Sym), capped at a sane limit —
 * not a blind 4096. For gcc/ld output .dynsym precedes .dynstr, so the
 * forward distance is the exact section size; the reversed-order case
 * uses the same distance magnitude so an unusual-but-valid layout still
 * gets a bounded (derived) count instead of a truncated scan. */
static unsigned long derive_symcount(unsigned long symtab,
                                     unsigned long strtab) {
    unsigned long span;
    if (symtab == 0 || strtab == 0 || strtab == symtab) return 0;
    span = (strtab > symtab) ? (strtab - symtab) : (symtab - strtab);
    span /= sizeof(elf64_sym);
    if (span > LDSO_SYM_COUNT_MAX) span = LDSO_SYM_COUNT_MAX;
    return span;
}

static void module_set_name(int idx, const char *name) {
    unsigned long i;
    for (i = 0; i < LDSO_NAME_MAX - 1 && name[i] != '\0'; i++) {
        g_modules[idx].name[i] = name[i];
    }
    g_modules[idx].name[i] = '\0';
}

/* Claim a free table slot. The slot is EMPTY until module_init() marks
 * it LOADING, so a failed init leaves no half-valid entry behind. */
static int add_module(const char *name) {
    int m;
    for (m = 0; m < LDSO_MAX_MODULES; m++) {
        if (g_modules[m].state == LDSO_MOD_EMPTY) {
            module_set_name(m, name);
            return m;
        }
    }
    return -1;
}

static void drop_module(int idx) {
    g_modules[idx].state = LDSO_MOD_EMPTY;
    g_modules[idx].name[0] = '\0';
}

static int find_module_by_name(const char *name) {
    int m;
    for (m = 0; m < LDSO_MAX_MODULES; m++) {
        if (g_modules[m].state == LDSO_MOD_EMPTY) continue;
        if (streq(g_modules[m].name, name)) return m;
    }
    return -1;
}

static int find_module_by_base(unsigned long base) {
    int m;
    for (m = 0; m < LDSO_MAX_MODULES; m++) {
        if (g_modules[m].state == LDSO_MOD_EMPTY) continue;
        if (g_modules[m].base == base) return m;
    }
    return -1;
}

/* BUG-0213 FIX: global symbol lookup — search EVERY loaded module's
 * .dynsym in load order (first loaded wins, i.e. the kernel-pre-mapped
 * libfoo.so interposes over dlopen'd copies of the same name). An
 * SHN_UNDEF match does NOT end the search; the name may be defined in a
 * later module. Returns 1 and sets *out_value and *out_size when a
 * defined symbol is found, 0 otherwise. */
static int modules_lookup(const char *name,
                          unsigned long *out_value,
                          unsigned long *out_size) {
    int m;
    for (m = 0; m < LDSO_MAX_MODULES; m++) {
        const struct ldso_module *mod = &g_modules[m];
        const elf64_sym *sym;
        unsigned long j;
        if (mod->state == LDSO_MOD_EMPTY) continue;
        if (mod->symtab == 0 || mod->strtab == 0 || mod->symcount == 0) {
            continue;
        }
        sym = (const elf64_sym *)mod->symtab;
        for (j = 0; j < mod->symcount; j++) {
            unsigned int name_off = sym[j].st_name;
            const char *sname;
            if (name_off == 0) continue;
            sname = (const char *)(mod->strtab + name_off);
            if (streq(sname, name)) {
                if (sym[j].st_shndx == SHN_UNDEF) continue;
                *out_value = mod->base + sym[j].st_value;
                *out_size  = sym[j].st_size;
                return 1;
            }
        }
    }
    return 0;
}

/* COPY-relocation lookup: identical to modules_lookup but EXCLUDES the
 * module that owns the COPY target. An executable's own .dynsym entry
 * for a COPY symbol is DEFINED (st_value = the target's bss address,
 * content zeros before the copy) - searching it would "copy" the target
 * onto itself and leave the variable zeroed (reloc_test regression:
 * foo_global read 0 instead of 42). A COPY source must come from some
 * OTHER module (the defining library). */
static int modules_lookup_except(const char *name, unsigned long except_base,
                                 unsigned long *out_value,
                                 unsigned long *out_size) {
    int m;
    for (m = 0; m < LDSO_MAX_MODULES; m++) {
        const struct ldso_module *mod = &g_modules[m];
        const elf64_sym *sym;
        unsigned long j;
        if (mod->state == LDSO_MOD_EMPTY) continue;
        if (mod->base == except_base) continue;
        if (mod->symtab == 0 || mod->strtab == 0 || mod->symcount == 0) {
            continue;
        }
        sym = (const elf64_sym *)mod->symtab;
        for (j = 0; j < mod->symcount; j++) {
            unsigned int name_off = sym[j].st_name;
            const char *sname;
            if (name_off == 0) continue;
            sname = (const char *)(mod->strtab + name_off);
            if (streq(sname, name)) {
                if (sym[j].st_shndx == SHN_UNDEF) continue;
                *out_value = mod->base + sym[j].st_value;
                *out_size  = sym[j].st_size;
                return 1;
            }
        }
    }
    return 0;
}

/* Validate + parse one ELF into a table slot. require_dynamic: the main
 * program and the pre-mapped libfoo must have a PT_DYNAMIC (failure is
 * fatal with a message); dlopen'd data-only objects are tolerated. All
 * program-header walking is bounded by elf64_sane() (BUG-0215 FIX) and
 * the phdr table must lie inside a mapped PT_LOAD file image (the
 * kernel maps PT_LOAD bytes only — anything else would be unreadable
 * and fail-closed here). */
static int module_init(int idx, const char *name, unsigned long base,
                       int require_dynamic) {
    struct ldso_module *mod = &g_modules[idx];
    const elf64_hdr *eh = (const elf64_hdr *)base;
    unsigned long phdr_bytes;
    int i;
    int have_load = 0;
    int phdrs_covered = 0;

    module_set_name(idx, name);
    mod->base = base;

    if (!elf64_sane(eh)) {
        puts_str("ld.so: module '");
        puts_str(mod->name);
        puts_str("': invalid ELF (magic/ELF64/ET_DYN/phdr bounds)\n");
        return -1;
    }
    phdr_bytes = (unsigned long)eh->phnum * sizeof(elf64_phdr);
    mod->phdrs = (elf64_phdr *)(base + eh->phoff);
    mod->phnum = eh->phnum;
    for (i = 0; i < (int)eh->phnum; i++) {
        const elf64_phdr *ph = &mod->phdrs[i];
        if (ph->type != PT_LOAD) continue;
        have_load = 1;
        if (eh->phoff + phdr_bytes <= ph->offset + ph->filesz) {
            phdrs_covered = 1;
        }
    }
    if (!have_load) {
        puts_str("ld.so: module '");
        puts_str(mod->name);
        puts_str("': has no PT_LOAD segment\n");
        return -1;
    }
    if (!phdrs_covered) {
        puts_str("ld.so: module '");
        puts_str(mod->name);
        puts_str("': program headers outside mapped image\n");
        return -1;
    }

    mod->dyn = find_dynamic(eh, base);
    if (mod->dyn == 0) {
        if (require_dynamic) {
            puts_str("ld.so: module '");
            puts_str(mod->name);
            puts_str("': has no PT_DYNAMIC\n");
            return -1;
        }
        /* Data-only shared object: registered, contributes no symbols. */
        mod->state = LDSO_MOD_LOADING;
        return 0;
    }
    dyn_scan(mod->dyn, base, &mod->strtab, &mod->strsz, &mod->symtab,
             &mod->rela, &mod->relasz, &mod->jmprel, &mod->pltrelsz);
    mod->symcount = derive_symcount(mod->symtab, mod->strtab);
    /* Registered + parsed but not yet finished: mark LOADING so the
     * module is visible to find_module_by_name (cycle detection) and
     * modules_lookup (its defined symbols resolve even before its own
     * relocations run), while finish_module() still knows it has work
     * left to do. */
    mod->state = LDSO_MOD_LOADING;
    return 0;
}

/* Generalized relocation processor.
 *
 *   rela / count : .rela.dyn or .rela.plt of the module being relocated
 *   base         : load base of that module (randomized)
 *   symtab_addr /
 *   symcount /
 *   strtab_addr /
 *   strsz        : the module's own .dynsym/.dynstr (name + binding of
 *                  each relocated symbol)
 *   modname      : for error messages
 *
 * Symbol VALUES are looked up in the global scope (all loaded modules).
 * BUG-0213 FIX: an unresolved strong (non-weak) symbol is a hard error
 * — the load aborts instead of silently writing 0 + addend. Undefined
 * WEAK symbols keep the spec behavior (0 + r_addend).
 * Returns 0 on success, -1 on a fail-closed condition. */
static int process_relocations(const elf64_rela *rela, unsigned long count,
                               unsigned long base,
                               unsigned long symtab_addr,
                               unsigned long symcount,
                               unsigned long strtab_addr,
                               unsigned long strsz,
                               const char *modname) {
    unsigned long k;
    for (k = 0; k < count; k++) {
        unsigned long rtype = rela[k].r_info & 0xffffffffUL;
        unsigned long sym_idx = rela[k].r_info >> 32;
        unsigned long target = base + rela[k].r_offset;
        unsigned long value = 0;

        if (rtype == R_X86_64_RELATIVE) {
            /* RELATIVE: just base + addend. No symbol lookup. */
            value = base + (unsigned long)rela[k].r_addend;
            *((unsigned long *)target) = value;
            continue;
        }

        if (rtype == R_X86_64_GLOB_DAT ||
            rtype == R_X86_64_JUMP_SLOT ||
            rtype == R_X86_64_64) {
            const elf64_sym *sym = (const elf64_sym *)symtab_addr;
            unsigned int name_off;
            unsigned char bind;
            const char *name;
            unsigned long sym_value = 0;
            unsigned long sym_size = 0;

            if (symtab_addr == 0 || sym_idx >= symcount) {
                puts_str("ld.so: symbol reloc with bad index in '");
                puts_str(modname);
                puts_str("'\n");
                return -1;
            }
            name_off = sym[sym_idx].st_name;
            bind = (unsigned char)(sym[sym_idx].st_info >> 4);
            if (strsz == 0 || name_off >= strsz) {
                puts_str("ld.so: symbol name outside strtab in '");
                puts_str(modname);
                puts_str("'\n");
                return -1;
            }
            name = (const char *)(strtab_addr + name_off);

            if (modules_lookup(name, &sym_value, &sym_size)) {
                value = sym_value + (unsigned long)rela[k].r_addend;
            } else if (bind == STB_WEAK) {
                /* Undefined weak: write 0 + addend (spec behavior). */
                value = (unsigned long)rela[k].r_addend;
            } else {
                /* Undefined strong: fail the load (BUG-0213 FIX). */
                puts_str("ld.so: unresolved strong symbol '");
                puts_str(name);
                puts_str("' required by '");
                puts_str(modname);
                puts_str("'\n");
                return -1;
            }
            *((unsigned long *)target) = value;
            continue;
        }

        if (rtype == R_X86_64_COPY) {
            const elf64_sym *sym = (const elf64_sym *)symtab_addr;
            unsigned int name_off;
            unsigned char bind;
            const char *name;
            unsigned long sym_value = 0;
            unsigned long sym_size = 0;

            if (symtab_addr == 0 || sym_idx >= symcount) {
                puts_str("ld.so: COPY reloc with bad index in '");
                puts_str(modname);
                puts_str("'\n");
                return -1;
            }
            name_off = sym[sym_idx].st_name;
            bind = (unsigned char)(sym[sym_idx].st_info >> 4);
            if (strsz == 0 || name_off >= strsz) {
                puts_str("ld.so: COPY name outside strtab in '");
                puts_str(modname);
                puts_str("'\n");
                return -1;
            }
            name = (const char *)(strtab_addr + name_off);

            if (!modules_lookup_except(name, base, &sym_value, &sym_size)) {
                if (bind == STB_WEAK) continue;  /* leave the slot zero */
                puts_str("ld.so: unresolved strong COPY symbol '");
                puts_str(name);
                puts_str("' required by '");
                puts_str(modname);
                puts_str("'\n");
                return -1;
            }
            if (sym_size == 0) {
                puts_str("ld.so: COPY reloc for '");
                puts_str(name);
                puts_str("' has zero size\n");
                return -1;
            }
            /* Copy sym_size bytes from the source module into
             * base + r_offset (= target). */
            {
                unsigned long b;
                char *dst = (char *)target;
                const char *sp = (const char *)sym_value;
                for (b = 0; b < sym_size; b++) {
                    dst[b] = sp[b];
                }
            }
            continue;
        }

        /* Unknown relocation type: print and skip. */
        puts_str("ld.so: unknown reloc type ");
        {
            char tbuf[16];
            int ti = 0;
            unsigned long t = rtype;
            if (t == 0) { tbuf[ti++] = '0'; }
            while (t > 0 && ti < 15) {
                tbuf[ti++] = (char)('0' + (t % 10));
                t /= 10;
            }
            int x;
            for (x = ti - 1; x >= 0; x--) {
                sys_write(1, &tbuf[x], 1);
            }
        }
        puts_str(" at 0x");
        puts_hex(target);
        puts_str("\n");
    }
    return 0;
}

/* Relocate one module (.rela.dyn then .rela.plt) against the global
 * scope. Returns 0 / -1. */
static int relocate_module(int idx) {
    struct ldso_module *mod = &g_modules[idx];
    if (mod->rela != 0 && mod->relasz != 0 &&
        mod->symtab != 0 && mod->strtab != 0) {
        unsigned long count = mod->relasz / sizeof(elf64_rela);
        if (process_relocations((const elf64_rela *)mod->rela, count,
                                mod->base, mod->symtab, mod->symcount,
                                mod->strtab, mod->strsz,
                                mod->name) != 0) {
            return -1;
        }
    }
    if (mod->jmprel != 0 && mod->pltrelsz != 0 &&
        mod->symtab != 0 && mod->strtab != 0) {
        unsigned long count = mod->pltrelsz / sizeof(elf64_rela);
        if (process_relocations((const elf64_rela *)mod->jmprel, count,
                                mod->base, mod->symtab, mod->symcount,
                                mod->strtab, mod->strsz,
                                mod->name) != 0) {
            return -1;
        }
    }
    return 0;
}

/* BUG-0216 FIX (W^X): tighten one module's PT_LOAD permissions now that
 * ALL of its relocations are applied and nothing writes to its image
 * anymore:
 *   - executable, non-writable segment (text): mprotect(R|X)  — strips
 *     the kernel's blanket +W;
 *   - writable, non-executable segment (data/bss/GOT): mprotect(R|W) —
 *     the kernel's mprotect adds VMM_FLAG_NOEXEC when PROT_EXEC is not
 *     requested, so data becomes non-executable;
 *   - read-only segment: mprotect(R) — also NX;
 *   - a segment that is both W and X cannot be split per-page here, so
 *     it is left exactly as the kernel mapped it (stated limitation).
 * mprotect failures are reported but do NOT abort the load: this is a
 * hardening pass, not a correctness requirement. */
static void module_apply_wx(int idx) {
    struct ldso_module *mod = &g_modules[idx];
    unsigned long i;
    for (i = 0; i < mod->phnum; i++) {
        const elf64_phdr *ph = &mod->phdrs[i];
        unsigned long addr;
        unsigned long len;
        long prot;
        if (ph->type != PT_LOAD) continue;
        if (ph->memsz == 0) continue;
        if ((ph->flags & PF_X) && (ph->flags & PF_W)) {
            continue;  /* W+X segment: cannot enforce W^X without a split */
        }
        addr = (mod->base + ph->vaddr) & ~0xFFFUL;
        len = ((mod->base + ph->vaddr + ph->memsz + 0xFFFUL) & ~0xFFFUL) - addr;
        if (ph->flags & PF_X) {
            prot = PROT_READ | PROT_EXEC;           /* text: drop +W */
        } else if (ph->flags & PF_W) {
            prot = PROT_READ | PROT_WRITE;          /* data: NX */
        } else {
            prot = PROT_READ;                        /* rodata: NX */
        }
        if (sys_mprotect(addr, len, prot) != 0) {
            puts_str("ld.so: mprotect W^X failed for '");
            puts_str(mod->name);
            puts_str("'\n");
        }
    }
}

static int finish_module(int idx, int depth);

/* Load every DT_NEEDED dependency of module idx that is not registered
 * yet, via SYS_MAP_SOLIB (BUG-0213 FIX). The kernel accepts only names
 * from its embedded solib table and returns the mapped base (or 0) —
 * anything else fails closed with a clear message. Each new module is
 * fully finished (its own deps loaded, relocations applied, W^X set)
 * before the caller proceeds, so dependent modules are always ready
 * before their user is relocated. Cycles are broken by the per-module
 * state: an already-LOADING dependency is owned by an ancestor
 * finish_module() frame and is skipped here; the depth bound caps
 * runaway chains at LDSO_MAX_DEPTH. Returns 0 / -1. */
static int ensure_needed(int idx, int depth) {
    struct ldso_module *mod = &g_modules[idx];
    int i;
    if (mod->dyn == 0 || mod->strtab == 0) return 0;
    for (i = 0; i < LDSO_DYN_MAX; i++) {
        unsigned long off;
        const char *dep;
        int found;
        int nidx;
        long len;
        long newbase;

        if (mod->dyn[i].tag == DT_NULL) break;
        if (mod->dyn[i].tag != DT_NEEDED) continue;

        off = mod->dyn[i].val;
        if (mod->strsz == 0 || off >= mod->strsz) {
            puts_str("ld.so: module '");
            puts_str(mod->name);
            puts_str("': DT_NEEDED offset outside strtab\n");
            return -1;
        }
        dep = (const char *)(mod->strtab + off);

        found = find_module_by_name(dep);
        if (found >= 0) continue;  /* READY: done; LOADING: cycle, skip */

        if (depth >= LDSO_MAX_DEPTH) {
            puts_str("ld.so: dependency depth limit reached at '");
            puts_str(dep);
            puts_str("'\n");
            return -1;
        }
        len = strlen_(dep);
        if (len <= 0 || len >= LDSO_NAME_MAX) {
            puts_str("ld.so: bad DT_NEEDED name in '");
            puts_str(mod->name);
            puts_str("'\n");
            return -1;
        }
        newbase = sys_map_solib(dep, len, 0);
        if (newbase <= 0) {
            puts_str("ld.so: cannot load '");
            puts_str(dep);
            puts_str("' (required by '");
            puts_str(mod->name);
            puts_str("'): not available to the kernel\n");
            return -1;
        }
        nidx = add_module(dep);
        if (nidx < 0) {
            puts_str("ld.so: module table full (loading '");
            puts_str(dep);
            puts_str("')\n");
            return -1;
        }
        if (module_init(nidx, dep, (unsigned long)newbase, 0) != 0) {
            drop_module(nidx);  /* stays mapped; never searched again */
            return -1;
        }
        if (finish_module(nidx, depth + 1) != 0) return -1;
    }
    return 0;
}

/* Finish a module: load its missing DT_NEEDED dependencies, apply its
 * relocations against the global scope, then enforce W^X on its
 * segments. state LOADING -> READY; READY is idempotent. */
static int finish_module(int idx, int depth) {
    struct ldso_module *mod = &g_modules[idx];
    if (mod->state == LDSO_MOD_READY) return 0;
    if (ensure_needed(idx, depth) != 0) return -1;
    if (relocate_module(idx) != 0) return -1;
    module_apply_wx(idx);
    mod->state = LDSO_MOD_READY;
    return 0;
}

/* ---- ld.so dynamic-linker API (dlopen/dlsym/dlclose) ----
 *
 * These functions are exposed to user programs via the API table at
 * 0x08000000. Marked noinline+used so they get stable addresses even
 * though they're only reached through function pointers. */

/* dlopen: ask the kernel to map a .so by name (base comes from the
 * kernel's per-process bump allocator). The new module is registered in
 * the loaded-module table, validated (BUG-0215 FIX), relocated against
 * ALL loaded modules and W^X-hardened before the handle is returned.
 * Returns the base address (or 0 on failure). */
__attribute__((noinline, used))
static void *ldso_dlopen(const char *name, int flags) {
    long len;
    long base;
    int idx;

    if (name == 0) return 0;
    len = strlen_(name);
    if (len <= 0 || len >= LDSO_NAME_MAX) return 0;
    base = sys_map_solib(name, len, flags);
    if (base <= 0) return 0;

    idx = add_module(name);
    if (idx < 0) return 0;
    if (module_init(idx, name, (unsigned long)base, 0) != 0) {
        drop_module(idx);
        return 0;
    }
    if (finish_module(idx, 0) != 0) {
        drop_module(idx);
        return 0;
    }
    return (void *)base;
}

/* dlsym: walk the HANDLE module's .dynsym, find name, return
 * base + st_value. handle is the base address returned by dlopen.
 * The walk bound is the module's derived symcount (BUG-0213 FIX), not a
 * blind 4096. Handles are resolved through the module table when
 * registered; otherwise the ELF is validated and parsed on the fly
 * (with the same BUG-0215 bounds). */
__attribute__((noinline, used))
static void *ldso_dlsym(void *handle, const char *name) {
    unsigned long base = (unsigned long)handle;
    unsigned long symtab_addr = 0;
    unsigned long strtab_addr = 0;
    unsigned long count = 0;
    const elf64_sym *sym;
    unsigned long j;
    int m;

    if (base == 0 || name == 0) return 0;

    /* Basic sanity: must look like an ELF64. */
    {
        const elf64_hdr *ehdr = (const elf64_hdr *)base;
        if (ehdr->ident[0] != 0x7f || ehdr->ident[1] != 'E' ||
            ehdr->ident[2] != 'L'  || ehdr->ident[3] != 'F') {
            return 0;
        }
        if (ehdr->ident[4] != 2) return 0;  /* ELF64 */
    }

    m = find_module_by_base(base);
    if (m >= 0) {
        symtab_addr = g_modules[m].symtab;
        strtab_addr = g_modules[m].strtab;
        count = g_modules[m].symcount;
    } else {
        /* Unknown handle: parse it directly, with the same bounds. */
        const elf64_hdr *ehdr = (const elf64_hdr *)base;
        elf64_dyn *dyn;
        if (!elf64_sane(ehdr)) return 0;
        dyn = find_dynamic(ehdr, base);
        if (dyn == 0) return 0;
        {
            unsigned long strsz = 0, rela = 0, relasz = 0;
            unsigned long jmprel = 0, pltrelsz = 0;
            dyn_scan(dyn, base, &strtab_addr, &strsz, &symtab_addr,
                     &rela, &relasz, &jmprel, &pltrelsz);
        }
        count = derive_symcount(symtab_addr, strtab_addr);
    }
    if (symtab_addr == 0 || strtab_addr == 0 || count == 0) return 0;

    sym = (const elf64_sym *)symtab_addr;
    for (j = 0; j < count; j++) {
        unsigned int name_off = sym[j].st_name;
        const char *sname;
        if (name_off == 0) continue;
        sname = (const char *)(strtab_addr + name_off);
        if (streq(sname, name)) {
            if (sym[j].st_shndx == SHN_UNDEF) return 0;
            return (void *)(base + sym[j].st_value);
        }
    }
    return 0;
}

/* dlclose: no-op (kernel keeps mappings until process exit). */
__attribute__((noinline, used))
static int ldso_dlclose(void *handle) {
    (void)handle;
    return 0;
}

/* API table layout exposed at 0x08000000 (offsets 0/8/16; offsets 24/32
 * are reserved for the kernel-written randomized load bases). */
struct ldso_api {
    void *(*dlopen)(const char *, int);
    void *(*dlsym)(void *, const char *);
    int   (*dlclose)(void *);
};

/* BUG-0214 FIX: return trampoline for the main program's entry.
 * Placed on the initial stack as main's "return address"; when the main
 * program's entry RETs, control lands here with main's return value in
 * %eax — the stub issues SYS_EXIT2 with it, so `ret` from main exits
 * the process cleanly instead of jumping into garbage. Emitted as
 * top-level asm so %rax can be read at the exact ABI point. */
__asm__(
    ".text\n"
    ".globl ldso_main_return\n"
    "ldso_main_return:\n\t"
    "movl %eax, %edi\n\t"      /* exit code = main's return value */
    "movl $16, %eax\n\t"       /* SYS_EXIT2 = 16 */
    "int $0x80\n"
    "ldso_main_hang:\n\t"
    "jmp ldso_main_hang\n"
);
extern void ldso_main_return(void);

/* SysV initial process stack image (BUG-0214 FIX).
 *
 * The kernel enters ld.so with a bare stack (no argv anywhere — its
 * launcher provides rsp only, even for static programs), so ld.so
 * builds the process image itself, following the same layout
 * sys_execve uses: [rsp]=argc, argv[] + NULL above it, envp[] + NULL,
 * then the auxv pairs. This program receives no argv from the kernel,
 * so argc=0 (argv[0]=NULL) with one empty envp[] and a minimal auxv
 * (AT_PAGESZ, AT_NULL). The image is written ~512 bytes below the
 * entry rsp — clear of _start's frame and any red zone — and the final
 * rsp is 16-byte aligned so that after the trampoline push, the main
 * program's entry sees rsp % 16 == 8 exactly as the SysV ABI requires. */
struct ldso_start_image {
    unsigned long argc;             /* 0 */
    unsigned long argv_null;        /* argv[0] = NULL (argc==0 sentinel) */
    unsigned long envp_null;        /* envp[0] = NULL ("at least one empty") */
    unsigned long auxv_pagesz_tag;  /* AT_PAGESZ */
    unsigned long auxv_pagesz_val;  /* 4096 */
    unsigned long auxv_null_tag;    /* AT_NULL */
    unsigned long auxv_null_val;    /* 0 */
};

static unsigned long build_sysv_stack(unsigned long entry_rsp) {
    unsigned long top = (entry_rsp - 512) & ~0xFUL;
    struct ldso_start_image *img =
        (struct ldso_start_image *)((top - sizeof(*img)) & ~0xFUL);
    img->argc = 0;
    img->argv_null = 0;
    img->envp_null = 0;
    img->auxv_pagesz_tag = AT_PAGESZ;
    img->auxv_pagesz_val = 4096;
    img->auxv_null_tag = AT_NULL;
    img->auxv_null_val = 0;
    return (unsigned long)img;
}

void _start(void) {
    unsigned long entry_rsp;
    unsigned long main_base;
    unsigned long libfoo_base;
    elf64_hdr *ehdr;
    int main_idx;
    int libfoo_idx;

    __asm__ volatile ("mov %%rsp, %0" : "=r"(entry_rsp));

    /* P4 fix: ld.so was printing verbose relocation info on every run.
     * It stays silent on success and only prints on error. */

    /* --- Read the kernel-randomized load bases from the API page ---
     * (BUG-0216 FIX: ld.so owns no fixed base anymore; the kernel
     * randomizes within the reserved regions and publishes the values
     * here. Fail closed if the slots are absent or out of region.) */
    main_base = *(volatile unsigned long *)(LDSO_API_TABLE_ADDR
                                           + LDSO_API_MAIN_BASE_SLOT);
    libfoo_base = *(volatile unsigned long *)(LDSO_API_TABLE_ADDR
                                             + LDSO_API_LIBFOO_BASE_SLOT);
    if (main_base < MAIN_REGION_BASE || main_base >= MAIN_REGION_END ||
        (main_base & 0xFFFUL) != 0) {
        puts_str("ld.so: bad main base 0x");
        puts_hex(main_base);
        puts_str("\n");
        sys_exit2(1);
    }
    if (libfoo_base < LIB_REGION_BASE || libfoo_base >= LIB_REGION_END ||
        (libfoo_base & 0xFFFUL) != 0) {
        puts_str("ld.so: bad libfoo.so base 0x");
        puts_hex(libfoo_base);
        puts_str("\n");
        sys_exit2(1);
    }

    /* --- Validate main ELF header (granular messages preserved) --- */
    ehdr = (elf64_hdr *)main_base;
    if (ehdr->ident[0] != 0x7f || ehdr->ident[1] != 'E' ||
        ehdr->ident[2] != 'L'  || ehdr->ident[3] != 'F') {
        puts_str("ld.so: not an ELF\n");
        sys_exit2(1);
    }
    if (ehdr->ident[4] != 2) {
        puts_str("ld.so: not ELF64\n");
        sys_exit2(1);
    }
    if (ehdr->type != ET_DYN) {
        puts_str("ld.so: not ET_DYN\n");
        sys_exit2(1);
    }

    /* --- Register main in the loaded-module table ---
     * module_init re-checks the header with full bounds (BUG-0215 FIX:
     * the phnum walk here is bounded now too) and parses .dynamic. */
    main_idx = add_module("main");
    if (main_idx < 0) {
        puts_str("ld.so: module table full (main)\n");
        sys_exit2(1);
    }
    if (module_init(main_idx, "main", main_base, 1) != 0) {
        sys_exit2(1);
    }
    if (g_modules[main_idx].strtab == 0) {
        puts_str("ld.so: no DT_STRTAB\n");
        sys_exit2(1);
    }

    /* --- Validate libfoo.so (kernel pre-maps it; base from API page) --- */
    {
        elf64_hdr *libfoo_ehdr = (elf64_hdr *)libfoo_base;
        if (libfoo_ehdr->ident[0] != 0x7f || libfoo_ehdr->ident[1] != 'E' ||
            libfoo_ehdr->ident[2] != 'L'  || libfoo_ehdr->ident[3] != 'F') {
            puts_str("ld.so: libfoo.so is not an ELF\n");
            sys_exit2(1);
        }
        if (libfoo_ehdr->ident[4] != 2) {
            puts_str("ld.so: libfoo.so is not ELF64\n");
            sys_exit2(1);
        }
        if (libfoo_ehdr->type != ET_DYN) {
            puts_str("ld.so: libfoo.so is not ET_DYN\n");
            sys_exit2(1);
        }
    }
    libfoo_idx = add_module("libfoo.so");
    if (libfoo_idx < 0) {
        puts_str("ld.so: module table full (libfoo.so)\n");
        sys_exit2(1);
    }
    if (module_init(libfoo_idx, "libfoo.so", libfoo_base, 1) != 0) {
        sys_exit2(1);
    }
    if (g_modules[libfoo_idx].symtab == 0 ||
        g_modules[libfoo_idx].strtab == 0) {
        puts_str("ld.so: libfoo.so missing DT_SYMTAB or DT_STRTAB\n");
        sys_exit2(1);
    }

    /* --- Finish libfoo, then main (BUG-0213 FIX) ---
     * finish_module() loads each module's DT_NEEDED dependencies first
     * (recursive, depth-bounded, via SYS_MAP_SOLIB — libfoo.so is
     * already in the table, so the common DT_NEEDED case is a no-op),
     * then relocates the module against ALL loaded modules, then
     * applies W^X (BUG-0216 FIX). A strong unresolved symbol anywhere
     * aborts with a message instead of silently resolving to 0. */
    if (finish_module(libfoo_idx, 0) != 0) {
        puts_str("ld.so: failed to relocate libfoo.so\n");
        sys_exit2(1);
    }
    if (finish_module(main_idx, 0) != 0) {
        puts_str("ld.so: failed to relocate main program\n");
        sys_exit2(1);
    }

    /* --- Install API table at 0x08000000 (fixed, layout unchanged) --- */
    {
        struct ldso_api *api = (struct ldso_api *)LDSO_API_TABLE_ADDR;
        api->dlopen  = ldso_dlopen;
        api->dlsym   = ldso_dlsym;
        api->dlclose = ldso_dlclose;
    }

    /* --- Enter the main program's entry with a SysV initial stack --- */
    {
        unsigned long main_entry = main_base + ehdr->entry;
        unsigned long new_rsp = build_sysv_stack(entry_rsp);
        /* Switch to the fresh image, push the return trampoline as the
         * entry's "return address", and jump. After the push rsp % 16
         * == 8 (correct SysV entry state); a `ret` from the main
         * program lands on ldso_main_return which exits cleanly. */
        __asm__ volatile (
            "mov %0, %%rsp\n\t"
            "push %2\n\t"
            "jmp *%1\n"
            "1:\tjmp 1b\n"
            :
            : "r"(new_rsp), "r"(main_entry),
              "r"((unsigned long)&ldso_main_return)
            : "memory"
        );
        for (;;) { }  /* not reached */
    }
}
