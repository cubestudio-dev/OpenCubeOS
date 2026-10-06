/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */

/* Open Cube OS WP-08b Batch 5 - ld.so generalized dynamic linker.
 *
 * The kernel maps:
 *   - ld.so (this program) at 0x10000000 (R+E, single PT_LOAD)
 *   - main program ELF (PIE bytes) at 0x20000000 (R+W+E, all PT_LOADs)
 *   - libfoo.so (PIE bytes) at 0x30000000 (R+W+E, all PT_LOADs)
 *   - API table page at 0x08000000 (R+W, 1 page) for ld.so to publish
 *     dlopen/dlsym/dlclose function pointers.
 *
 * _start flow:
 *   1. Print "ld.so started"
 *   2. Validate main ELF at 0x20000000 (magic/ELF64/ET_DYN)
 *   3. Find PT_DYNAMIC, find DT_STRTAB/DT_SYMTAB/DT_NEEDED -> print names
 *   4. Validate libfoo.so at 0x30000000 (magic/ELF64/ET_DYN/phoff)
 *   5. Parse libfoo.so .dynamic -> find DT_SYMTAB/DT_STRTAB
 *   6. Process libfoo.so .rela.dyn (RELATIVE + GLOB_DAT for undef weaks)
 *   7. Process main's .rela.dyn (if DT_RELA present)
 *   8. Process main's .rela.plt (if DT_JMPREL present)
 *   9. Install API table at 0x08000000 (dlopen/dlsym/dlclose ptrs)
 *  10. Print "jumping to main entry at 0x<addr>" and jmp there
 *
 * The generalized process_relocations() handles R_X86_64_RELATIVE,
 * R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT, R_X86_64_COPY.
 * Symbol lookups by name go through libfoo.so's .dynsym (the only .so
 * in the system); undefined-weak symbols resolve to 0 + r_addend.
 */

/* Syscall numbers (per kernel/syscall.h) */
#define SYS_WRITE      1
#define SYS_EXIT2    16
#define SYS_MAP_SOLIB 90

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
#define MAIN_ELF_BASE 0x20000000UL
#define LIBFOO_BASE   0x30000000UL
#define LDSO_API_TABLE_ADDR 0x08000000UL

#define ET_DYN       3
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

/* SHN_UNDEF: symbol is undefined (declared but not defined in this module). */
#define SHN_UNDEF 0

/* BUG-018/044 FIX: Global variables for libfoo info, accessible from
 * ldso_dlopen for relocation processing of dlopen'd libraries. */
static unsigned long g_libfoo_base = 0;
static unsigned long g_libfoo_symtab = 0;
static unsigned long g_libfoo_strtab = 0;

/* Find PT_DYNAMIC in an ELF's program headers. Returns 0 on failure. */
static elf64_dyn *find_dynamic(elf64_hdr *ehdr, unsigned long base) {
    elf64_phdr *phdr = (elf64_phdr *)(base + ehdr->phoff);
    int i;
    for (i = 0; i < ehdr->phnum; i++) {
        if (phdr[i].type == PT_DYNAMIC) {
            return (elf64_dyn *)(base + phdr[i].vaddr);
        }
    }
    return 0;
}

/* Look up a symbol by name in libfoo.so's .dynsym.
 * Returns 1 if found and defined (sets *out_value = libfoo_base + st_value,
 * and *out_size = st_size). Returns 0 if not found or undefined-weak. */
static int libfoo_lookup(const char *name,
                         unsigned long libfoo_symtab,
                         unsigned long libfoo_strtab,
                         unsigned long libfoo_base,
                         unsigned long *out_value,
                         unsigned long *out_size) {
    elf64_sym *sym = (elf64_sym *)libfoo_symtab;
    int j;
    /* BUG-044 FIX: Iterate up to 4096 entries (was hardcoded 64).
     * Don't break on null entry at index 0 (it's the standard ELF
     * undefined entry, not a terminator). Just skip entries with
     * st_name==0 (they're either the null entry or unused). */
    for (j = 0; j < 4096; j++) {
        unsigned int name_off = sym[j].st_name;
        if (name_off == 0) continue;
        const char *sname = (const char *)(libfoo_strtab + name_off);
        if (streq(sname, name)) {
            /* Found. If the symbol is undefined (SHN_UNDEF), it's an
             * undefined weak -- treat as "not found" so the caller writes
             * 0 + r_addend. */
            if (sym[j].st_shndx == SHN_UNDEF) {
                return 0;
            }
            *out_value = libfoo_base + sym[j].st_value;
            *out_size  = sym[j].st_size;
            return 1;
        }
    }
    return 0;
}

/* Generalized relocation processor.
 *
 *   rela          : pointer to first Elf64_Rela entry
 *   count         : number of entries (size_bytes / 24)
 *   base          : load base of the binary being relocated (e.g. libfoo_base
 *                   for libfoo's own .rela.dyn, MAIN_ELF_BASE for main's)
 *   symtab_addr   : .dynsym address of the binary being relocated (used to
 *                   fetch symbol NAMES from r_info's symbol index)
 *   strtab_addr   : .dynstr address of the binary being relocated
 *   libfoo_base   : libfoo.so load base (for value lookup)
 *   libfoo_symtab : libfoo.so .dynsym address (for value lookup)
 *   libfoo_strtab : libfoo.so .dynstr address (for value lookup)
 *
 * For each entry, dispatch on type:
 *   RELATIVE   : write base + addend
 *   GLOB_DAT /
 *   JUMP_SLOT /
 *   R_X86_64_64: look up name in libfoo.so .dynsym; if found and defined,
 *                write libfoo_base + st_value + addend; else write 0 + addend
 *   COPY       : look up name in libfoo.so .dynsym; if found, copy
 *                st_size bytes from libfoo_base + st_value to base + r_offset
 */
static void process_relocations(elf64_rela *rela, unsigned long count,
                                unsigned long base,
                                unsigned long symtab_addr,
                                unsigned long strtab_addr,
                                unsigned long libfoo_base,
                                unsigned long libfoo_symtab,
                                unsigned long libfoo_strtab) {
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
            /* P4 fix: was putting verbose per-reloc info here. */
            continue;
        }

        if (rtype == R_X86_64_GLOB_DAT ||
            rtype == R_X86_64_JUMP_SLOT ||
            rtype == R_X86_64_64) {
            /* Get symbol name from the relocating binary's .dynsym. */
            elf64_sym *sym = (elf64_sym *)symtab_addr;
            unsigned int name_off = sym[sym_idx].st_name;
            const char *name = (const char *)(strtab_addr + name_off);

            unsigned long sym_value = 0;
            unsigned long sym_size = 0;
            int found = libfoo_lookup(name, libfoo_symtab, libfoo_strtab,
                                      libfoo_base, &sym_value, &sym_size);
            if (found) {
                value = sym_value + (unsigned long)rela[k].r_addend;
            } else {
                /* Undefined weak (or not found): write 0 + addend. */
                value = (unsigned long)rela[k].r_addend;
            }
            *((unsigned long *)target) = value;
            /* P4 fix: was putting verbose per-reloc info here. */
            continue;
        }

        if (rtype == R_X86_64_COPY) {
            elf64_sym *sym = (elf64_sym *)symtab_addr;
            unsigned int name_off = sym[sym_idx].st_name;
            const char *name = (const char *)(strtab_addr + name_off);

            unsigned long sym_value = 0;
            unsigned long sym_size = 0;
            int found = libfoo_lookup(name, libfoo_symtab, libfoo_strtab,
                                      libfoo_base, &sym_value, &sym_size);
            if (!found || sym_size == 0) {
                puts_str("ld.so: COPY reloc for '");
                puts_str(name);
                puts_str("' not found in libfoo.so\n");
                continue;
            }
            /* Copy sym_size bytes from libfoo_base + st_value (= sym_value)
             * to base + r_offset (= target). */
            unsigned long src = sym_value;
            char *dst = (char *)target;
            const char *sp = (const char *)src;
            unsigned long b;
            for (b = 0; b < sym_size; b++) {
                dst[b] = sp[b];
            }
            /* P4 fix: was putting verbose COPY info here. */
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
}

/* ---- ld.so dynamic-linker API (dlopen/dlsym/dlclose) ----
 *
 * These functions are exposed to user programs via the API table at
 * 0x08000000. Marked noinline+used so they get stable addresses even
 * though they're only reached through function pointers. */

/* dlopen: ask the kernel to map a .so by name. Returns base address
 * (or 0 on failure). The kernel maps at proc->next_solib_addr (bump
 * allocator from 0x50000000).
 * BUG-018 FIX: After mapping, process the .so's relocations (.rela.dyn
 * and .rela.plt) so that RELATIVE/GLOB_DAT/JUMP_SLOT entries are patched.
 * Old code just mapped the raw bytes without relocation. */
__attribute__((noinline, used))
static void *ldso_dlopen(const char *name, int flags) {
    long len = strlen_(name);
    long base;
    __asm__ volatile (
        "int $0x80"
        : "=a"(base)
        : "a"((long)SYS_MAP_SOLIB), "D"(name), "S"((unsigned long)len),
          "d"((long)flags)
        : "memory", "rcx", "r11"
    );
    if (base <= 0) return 0;

    /* BUG-018 FIX: Process relocations on the newly mapped .so. */
    elf64_hdr *eh = (elf64_hdr *)base;
    elf64_dyn *dyn = find_dynamic(eh, (unsigned long)base);
    if (dyn) {
        unsigned long so_symtab = 0, so_strtab = 0;
        unsigned long so_rela = 0, so_relasz = 0;
        unsigned long so_jmprel = 0, so_pltrelsz = 0;
        int i;
        /* BUG-0065 FIX: the walk used to rely solely on hitting DT_NULL.
         * A crafted .dynamic without a terminator makes i run away
         * reading kernel memory. Bound every walk to 64 entries
         * (far more than any real shared object needs). */
        for (i = 0; i < 64; i++) {
            if (dyn[i].tag == DT_NULL) break;
            if (dyn[i].tag == DT_SYMTAB) so_symtab = base + dyn[i].val;
            else if (dyn[i].tag == DT_STRTAB) so_strtab = base + dyn[i].val;
            else if (dyn[i].tag == DT_RELA) so_rela = base + dyn[i].val;
            else if (dyn[i].tag == DT_RELASZ) so_relasz = dyn[i].val;
            else if (dyn[i].tag == DT_JMPREL) so_jmprel = base + dyn[i].val;
            else if (dyn[i].tag == DT_PLTRELSZ) so_pltrelsz = dyn[i].val;
        }
        /* Process .rela.dyn */
        if (so_rela && so_relasz && so_symtab && so_strtab) {
            unsigned long count = so_relasz / sizeof(elf64_rela);
            process_relocations((elf64_rela *)so_rela, count,
                                (unsigned long)base, so_symtab, so_strtab,
                                g_libfoo_base, g_libfoo_symtab, g_libfoo_strtab);
        }
        /* Process .rela.plt */
        if (so_jmprel && so_pltrelsz && so_symtab && so_strtab) {
            unsigned long count = so_pltrelsz / sizeof(elf64_rela);
            process_relocations((elf64_rela *)so_jmprel, count,
                                (unsigned long)base, so_symtab, so_strtab,
                                g_libfoo_base, g_libfoo_symtab, g_libfoo_strtab);
        }
    }
    return (void *)base;
}

/* dlsym: walk handle's .dynsym, find name, return base + st_value.
 * handle is the base address returned by dlopen. */
__attribute__((noinline, used))
static void *ldso_dlsym(void *handle, const char *name) {
    unsigned long base = (unsigned long)handle;
    if (base == 0) return 0;

    elf64_hdr *ehdr = (elf64_hdr *)base;
    /* Basic sanity: must look like an ELF. */
    if (ehdr->ident[0] != 0x7f || ehdr->ident[1] != 'E' ||
        ehdr->ident[2] != 'L'  || ehdr->ident[3] != 'F') {
        return 0;
    }
    if (ehdr->ident[4] != 2) return 0;  /* ELF64 */

    elf64_dyn *dyn = find_dynamic(ehdr, base);
    if (!dyn) return 0;

    unsigned long symtab_addr = 0;
    unsigned long strtab_addr = 0;
    int i;
    for (i = 0; i < 64; i++) {
        if (dyn[i].tag == DT_NULL) break;
        if (dyn[i].tag == DT_SYMTAB) {
            symtab_addr = base + dyn[i].val;
        } else if (dyn[i].tag == DT_STRTAB) {
            strtab_addr = base + dyn[i].val;
        }
    }
    if (!symtab_addr || !strtab_addr) return 0;

    elf64_sym *sym = (elf64_sym *)symtab_addr;
    int j;
    /* BUG-044 FIX: Iterate up to 4096 entries (was hardcoded 64). */
    for (j = 0; j < 4096; j++) {
        unsigned int name_off = sym[j].st_name;
        if (name_off == 0) continue;
        const char *sname = (const char *)(strtab_addr + name_off);
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

/* API table layout exposed at 0x08000000. */
struct ldso_api {
    void *(*dlopen)(const char *, int);
    void *(*dlsym)(void *, const char *);
    int   (*dlclose)(void *);
};

void _start(void) {
    /* P4 fix: ld.so was printing verbose relocation info on every run
     * (one line per relocation + several informational lines). This
     * cluttered the console and intermixed with the main program's
     * output. Now ld.so is silent on success and only prints on error. */

    /* --- Validate main ELF header at 0x20000000 --- */
    elf64_hdr *ehdr = (elf64_hdr *)MAIN_ELF_BASE;
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

    /* --- Find PT_DYNAMIC + DT_STRTAB + DT_NEEDED in main --- */
    elf64_dyn *main_dyn = find_dynamic(ehdr, MAIN_ELF_BASE);
    if (!main_dyn) {
        puts_str("ld.so: main has no PT_DYNAMIC\n");
        sys_exit2(1);
    }
    unsigned long dyn_addr = (unsigned long)main_dyn;
    /* P4 fix: silenced — was puts_str("ld.so: PT_DYNAMIC at 0x..."); */
    (void)dyn_addr;

    unsigned long main_strtab_addr = 0;
    unsigned long main_symtab_addr = 0;
    int i;
    for (i = 0; i < 64; i++) {
        if (main_dyn[i].tag == DT_NULL) break;
        if (main_dyn[i].tag == DT_STRTAB) {
            main_strtab_addr = MAIN_ELF_BASE + main_dyn[i].val;
        } else if (main_dyn[i].tag == DT_SYMTAB) {
            main_symtab_addr = MAIN_ELF_BASE + main_dyn[i].val;
        }
    }
    if (!main_strtab_addr) {
        puts_str("ld.so: no DT_STRTAB\n");
        sys_exit2(1);
    }

    /* Print DT_NEEDED entries. */
    int needed_count = 0;
    for (i = 0; i < 64; i++) {
        if (main_dyn[i].tag == DT_NULL) break;
        if (main_dyn[i].tag == DT_NEEDED) {
            /* P4 fix: silenced — was puts_str("ld.so: DT_NEEDED: <name>"); */
            needed_count++;
        }
    }
    /* P4 fix: silenced — was puts_str("ld.so: (no DT_NEEDED entries)"); */

    /* --- Validate libfoo.so at 0x30000000 --- */
    elf64_hdr *libfoo_ehdr = (elf64_hdr *)LIBFOO_BASE;
    if (libfoo_ehdr->ident[0] != 0x7f || libfoo_ehdr->ident[1] != 'E' ||
        libfoo_ehdr->ident[2] != 'L'  || libfoo_ehdr->ident[3] != 'F') {
        puts_str("ld.so: libfoo.so at 0x30000000 is not an ELF\n");
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
    if (libfoo_ehdr->phoff < sizeof(elf64_hdr) ||
        libfoo_ehdr->phoff >= 0x1000) {
        puts_str("ld.so: libfoo.so has invalid phoff\n");
        sys_exit2(1);
    }
    /* P4 fix: silenced — was puts_str("ld.so: libfoo.so loaded at 0x..."); */

    /* --- Parse libfoo.so .dynamic -> DT_SYMTAB / DT_STRTAB --- */
    elf64_dyn *libfoo_dyn = find_dynamic(libfoo_ehdr, LIBFOO_BASE);
    if (!libfoo_dyn) {
        puts_str("ld.so: libfoo.so has no PT_DYNAMIC\n");
        sys_exit2(1);
    }
    unsigned long libfoo_symtab_addr = 0;
    unsigned long libfoo_strtab_addr = 0;
    unsigned long libfoo_rela_addr_vaddr = 0;   /* DT_RELA d_ptr (vaddr) */
    unsigned long libfoo_relasz = 0;            /* DT_RELASZ in bytes */
    for (i = 0; i < 64; i++) {
        if (libfoo_dyn[i].tag == DT_NULL) break;
        if (libfoo_dyn[i].tag == DT_SYMTAB) {
            libfoo_symtab_addr = LIBFOO_BASE + libfoo_dyn[i].val;
        } else if (libfoo_dyn[i].tag == DT_STRTAB) {
            libfoo_strtab_addr = LIBFOO_BASE + libfoo_dyn[i].val;
        } else if (libfoo_dyn[i].tag == DT_RELA) {
            libfoo_rela_addr_vaddr = libfoo_dyn[i].val;
        } else if (libfoo_dyn[i].tag == DT_RELASZ) {
            libfoo_relasz = libfoo_dyn[i].val;
        }
    }
    if (!libfoo_symtab_addr || !libfoo_strtab_addr) {
        puts_str("ld.so: libfoo.so missing DT_SYMTAB or DT_STRTAB\n");
        sys_exit2(1);
    }
    /* BUG-018 FIX: Set global libfoo info for ldso_dlopen relocation processing. */
    g_libfoo_base = LIBFOO_BASE;
    g_libfoo_symtab = libfoo_symtab_addr;
    g_libfoo_strtab = libfoo_strtab_addr;
    /* P4 fix: silenced — was puts_str("ld.so: libfoo.so .dynsym at 0x...");
     * was puts_str("ld.so: libfoo.so .dynstr at 0x..."); */

    /* --- Process libfoo.so's .rela.dyn (base = LIBFOO_BASE) --- */
    if (libfoo_rela_addr_vaddr != 0 && libfoo_relasz != 0) {
        elf64_rela *libfoo_rela =
            (elf64_rela *)(LIBFOO_BASE + libfoo_rela_addr_vaddr);
        unsigned long libfoo_rela_count = libfoo_relasz / 24;
        /* P4 fix: silenced — was puts_str("ld.so: libfoo.so .rela.dyn at 0x..."); */
        process_relocations(libfoo_rela, libfoo_rela_count, LIBFOO_BASE,
                            libfoo_symtab_addr, libfoo_strtab_addr,
                            LIBFOO_BASE, libfoo_symtab_addr,
                            libfoo_strtab_addr);
    } else {
        /* P4 fix: silenced — was puts_str("ld.so: libfoo.so has no .rela.dyn"); */
    }

    /* --- Process main's .rela.dyn (if DT_RELA present) --- */
    {
        unsigned long main_rela_vaddr = 0;
        unsigned long main_relasz = 0;
        for (i = 0; i < 64; i++) {
            if (main_dyn[i].tag == DT_NULL) break;
            if (main_dyn[i].tag == DT_RELA) {
                main_rela_vaddr = main_dyn[i].val;
            } else if (main_dyn[i].tag == DT_RELASZ) {
                main_relasz = main_dyn[i].val;
            }
        }
        if (main_rela_vaddr != 0 && main_relasz != 0) {
            elf64_rela *main_rela =
                (elf64_rela *)(MAIN_ELF_BASE + main_rela_vaddr);
            unsigned long main_rela_count = main_relasz / 24;
            /* P4 fix: silenced — was puts_str("ld.so: main .rela.dyn at 0x..."); */
            process_relocations(main_rela, main_rela_count, MAIN_ELF_BASE,
                                main_symtab_addr, main_strtab_addr,
                                LIBFOO_BASE, libfoo_symtab_addr,
                                libfoo_strtab_addr);
        } else {
            /* P4 fix: silenced — was puts_str("ld.so: (no .rela.dyn)"); */
        }
    }

    /* --- Process main's .rela.plt (if DT_JMPREL present) --- */
    {
        unsigned long main_jmprel_vaddr = 0;
        unsigned long main_pltrelsz = 0;
        for (i = 0; i < 64; i++) {
            if (main_dyn[i].tag == DT_NULL) break;
            if (main_dyn[i].tag == DT_JMPREL) {
                main_jmprel_vaddr = main_dyn[i].val;
            } else if (main_dyn[i].tag == DT_PLTRELSZ) {
                main_pltrelsz = main_dyn[i].val;
            }
        }
        if (main_jmprel_vaddr != 0 && main_pltrelsz != 0) {
            elf64_rela *main_plt =
                (elf64_rela *)(MAIN_ELF_BASE + main_jmprel_vaddr);
            unsigned long main_plt_count = main_pltrelsz / 24;
            /* P4 fix: silenced — was puts_str("ld.so: main .rela.plt at 0x..."); */
            process_relocations(main_plt, main_plt_count, MAIN_ELF_BASE,
                                main_symtab_addr, main_strtab_addr,
                                LIBFOO_BASE, libfoo_symtab_addr,
                                libfoo_strtab_addr);
        } else {
            /* P4 fix: silenced — was puts_str("ld.so: (no .rela.plt)"); */
        }
    }

    /* --- Install API table at 0x08000000 --- */
    {
        struct ldso_api *api = (struct ldso_api *)LDSO_API_TABLE_ADDR;
        api->dlopen  = ldso_dlopen;
        api->dlsym   = ldso_dlsym;
        api->dlclose = ldso_dlclose;
        /* P4 fix: silenced — was puts_str("ld.so: API table installed at 0x..."); */
    }

    /* --- Jump to main entry --- */
    {
        unsigned long main_entry = MAIN_ELF_BASE + ehdr->entry;
        /* P4 fix: silenced — was puts_str("ld.so: jumping to main entry at 0x..."); */
        __asm__ volatile (
            "jmp *%0\n"
            : : "r"(main_entry)
        );
        for (;;) { }
    }
}
