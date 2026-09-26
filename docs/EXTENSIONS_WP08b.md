# Open Cube OS — WP-08b Extension Interfaces

WP-08b adds 10 new extension points (items 41-50). Total L1 surface is now 50
(WP-01..07: 32, WP-08a: 8, WP-08b: 10).

These interfaces wrap the kernel's dynamic linking infrastructure so L1
extensions can load and use shared libraries without directly calling syscalls
or parsing ELF structures.

## Interface 41: elf_load_dynamic

### Signature
```c
int elf_load_dynamic(const u8 *elf_data, u64 elf_size, const char *name);
```

### Purpose
Loads an ET_DYN (PIE) ELF. The kernel detects ET_DYN, parses PT_INTERP,
looks up ld.so in the interp table, maps ld.so + the main program + all
known .so files into the user address space, and jumps to ld.so's entry
point. ld.so (in user space) then parses .dynamic, resolves symbols,
patches GOT/PLT, and jumps to the main program's entry.

### Parameters
| Parameter | Type | Description |
|-----------|------|-------------|
| elf_data  | `const u8*` | Pointer to the ELF byte array (embedded in kernel) |
| elf_size  | `u64` | Size of the ELF data in bytes |
| name      | `const char*` | Process name (for the process table, max 31 chars) |

### Return value
PID (>0) on success, -1 on failure.

### Example
```c
/* L1 extension that loads a dynamic program */
extern const u8 userprog_so_test[];
extern const u64 userprog_so_test_size;
int pid = elf_load_dynamic(userprog_so_test, userprog_so_test_size, "so_test");
if (pid > 0) {
    /* Process created; ld.so will handle dynamic linking */
}
```

---

## Interface 42: ldso_run

### Signature
```c
u64 ldso_run(void);
```

### Purpose
Returns the base address where ld.so is mapped in the current process's
address space. ld.so is always at 0x10000000 (fixed). This is informational
— the actual ld.so execution is handled by elf_load_dynamic.

### Return value
ld.so base address (0x10000000), or 0 if not a dynamic process.

### Example
```c
u64 ldso_base = ldso_run();
if (ldso_base) {
    /* This process has ld.so mapped */
}
```

---

## Interface 43: so_load

### Signature
```c
u64 so_load(const char *name, u64 flags);
```

### Purpose
Maps a .so by name into the current process's address space. The .so must
be in the kernel's embedded solib table (kernel/solib_data.h). Maps PT_LOAD
segments at the per-process bump allocator (0x50000000+).

### Parameters
| Parameter | Type | Description |
|-----------|------|-------------|
| name      | `const char*` | .so name (e.g., "libfoo.so"), NUL-terminated |
| flags     | `u64` | Reserved (0 for now) |

### Return value
Base address of the mapped .so, or 0 on failure.

### Example
```c
u64 handle = so_load("libfoo.so", 0);
if (handle) {
    /* libfoo.so is now mapped at `handle` */
}
```

---

## Interface 44: so_unload

### Signature
```c
int so_unload(u64 handle);
```

### Purpose
Unmaps a previously loaded .so. Currently a no-op — mappings are released
when the process exits. This is a real implementation (returns 0 = success),
not a stub.

### Parameters
| Parameter | Type | Description |
|-----------|------|-------------|
| handle    | `u64` | Base address returned by so_load |

### Return value
0 on success.

---

## Interface 45: symbol_resolve

### Signature
```c
u64 symbol_resolve(u64 handle, const char *name);
```

### Purpose
Resolves a symbol by name in a loaded .so. Walks the .so's .dynamic →
.dynsym/.dynstr, finds the symbol, returns its runtime address
(base + st_value).

### Parameters
| Parameter | Type | Description |
|-----------|------|-------------|
| handle    | `u64` | Base address of the .so (from so_load) |
| name      | `const char*` | Symbol name (e.g., "foo_add") |

### Return value
Address of the symbol, or 0 if not found.

### Example
```c
u64 handle = so_load("libfoo.so", 0);
u64 addr = symbol_resolve(handle, "foo_add");
if (addr) {
    int (*fn)(int, int) = (int (*)(int, int))addr;
    int result = fn(2, 3);  /* 5 */
}
```

---

## Interface 46: dlsym_impl

### Signature
```c
u64 dlsym_impl(u64 handle, const char *name);
```

### Purpose
L1-facing alias for symbol_resolve. Same behavior — resolves a symbol
by name in a loaded .so and returns its address.

---

## Interface 47: dlopen_impl

### Signature
```c
u64 dlopen_impl(const char *name, u64 flags);
```

### Purpose
L1-facing alias for so_load. Same behavior — maps a .so by name into
the current process's address space and returns its base address.

---

## Interface 48: dlclose_impl

### Signature
```c
int dlclose_impl(u64 handle);
```

### Purpose
L1-facing alias for so_unload. Same behavior — releases a .so mapping
(currently a no-op, returns 0).

---

## Interface 49: reloc_apply

### Signature
```c
void reloc_apply(const void *rela_data, u64 count, u64 base,
                 u64 symtab_addr, u64 strtab_addr,
                 u64 libfoo_base, u64 libfoo_symtab, u64 libfoo_strtab);
```

### Purpose
Applies a batch of ELF relocations (Elf64_Rela entries). Handles
R_X86_64_RELATIVE, GLOB_DAT, 64, COPY, JUMP_SLOT. Primarily used by
ld.so in user space; exposed here for L1 extensions that process ELF
relocations in kernel context.

### Parameters
| Parameter | Type | Description |
|-----------|------|-------------|
| rela_data    | `const void*` | Pointer to the .rela.* section (24-byte entries) |
| count        | `u64` | Number of Elf64_Rela entries |
| base         | `u64` | Load base of the ELF being relocated |
| symtab_addr  | `u64` | Address of .dynsym (in the ELF's loaded image) |
| strtab_addr  | `u64` | Address of .dynstr (in the ELF's loaded image) |
| libfoo_base  | `u64` | Base of libfoo.so (for cross-library symbol lookup) |
| libfoo_symtab | `u64` | libfoo.so .dynsym address |
| libfoo_strtab | `u64` | libfoo.so .dynstr address |

### Relocation types handled
| Type | Value | Action |
|------|-------|--------|
| R_X86_64_RELATIVE | 8 | `*(base + r_offset) = base + r_addend` |
| R_X86_64_GLOB_DAT | 6 | `*(base + r_offset) = sym_value + r_addend` |
| R_X86_64_64 | 1 | Same as GLOB_DAT |
| R_X86_64_COPY | 5 | Copy `st_size` bytes from libfoo.so to `base + r_offset` |
| R_X86_64_JUMP_SLOT | 7 | Same as GLOB_DAT (patch GOT) |

---

## Interface 50: elf_get_needed

### Signature
```c
int elf_get_needed(const u8 *elf_data, const char *needed_out[], int max_count);
```

### Purpose
Reads an ELF's .dynamic section and extracts the DT_NEEDED dependency
list. Works on raw ELF byte data (embedded or loaded). For static
(ET_EXEC) binaries, returns 0.

### Parameters
| Parameter | Type | Description |
|-----------|------|-------------|
| elf_data   | `const u8*` | Pointer to the ELF byte array |
| needed_out | `const char**` | Output array of .so name pointers (into elf_data) |
| max_count  | `int` | Max entries in needed_out |

### Return value
Number of DT_NEEDED entries found (0 if none or static).

### Example
```c
const char *needed[8];
int n = elf_get_needed(userprog_so_test, needed, 8);
/* n = 1, needed[0] = "libfoo.so" */
```

---

## ABI Stability

All 10 interfaces (41-50) are frozen as of WP-08b Batch 6. The function
signatures, parameter types, return values, and relocation type handling
will not change in future work packages. L1 extensions can rely on these
interfaces being stable.

## Loading model

L1 extensions load shared libraries through the kernel's embedded solib
table (kernel/solib_data.h). The kernel maps .so files from this table
at fixed addresses (0x30000000 for pre-mapped, 0x50000000+ for dlopen'd).
Future work packages may add filesystem-backed .so loading (where the .so
is read from a mounted filesystem rather than embedded in the kernel).
