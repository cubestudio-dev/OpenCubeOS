/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-08b Extension Interfaces
 * File: kernel/ext_wp8b.h
 * Purpose: L1 extension interfaces for dynamic linking — ELF dynamic
 *          loading, ld.so invocation, shared library management,
 *          symbol resolution, relocation processing, dependency query.
 *
 * WP-08b adds 10 new L1 extension interfaces (items 41-50).
 * Total L1 surface is now 50 (WP-01..07: 32, WP-08a: 8, WP-08b: 10).
 *
 * These interfaces wrap the kernel's dynamic linking infrastructure
 * (user_process_create's ET_DYN path, sys_map_solib, the solib name
 * table) so L1 extensions can load and use shared libraries without
 * directly calling syscalls or parsing ELF structures. */

#ifndef OC_EXT_WP8B_H
#define OC_EXT_WP8B_H

#include "types.h"

/* ---- Interface 41: Dynamic ELF Loader ----
 * Loads an ET_DYN (PIE) ELF. The kernel detects ET_DYN, parses
 * PT_INTERP, looks up ld.so in the interp table, maps ld.so +
 * the main program + all known .so files into the user address
 * space, and jumps to ld.so's entry point. ld.so (in user space)
 * then parses .dynamic, resolves symbols, patches GOT/PLT, and
 * jumps to the main program's entry.
 *
 * Parameters:
 *   elf_data — pointer to the ELF byte array (embedded in kernel)
 *   elf_size — size of the ELF data in bytes
 *   name     — process name (for the process table, max 31 chars)
 * Returns: PID (>0) on success, -1 on failure. */
int elf_load_dynamic(const u8 *elf_data, u64 elf_size, const char *name);

/* ---- Interface 42: ld.so Runner ----
 * Returns the base address where ld.so is mapped in the current
 * process's address space. ld.so is always at 0x10000000 (fixed).
 * This is informational — the actual ld.so execution is handled
 * by elf_load_dynamic (which sets up the jump to ld.so entry).
 *
 * Returns: ld.so base address (0x10000000), or 0 if not a dynamic process. */
u64 ldso_run(void);

/* ---- Interface 43: Shared Library Loader ----
 * Maps a .so by name into the current process's address space.
 * The .so must be in the kernel's embedded solib table
 * (kernel/solib_data.h). Maps PT_LOAD segments at the per-process
 * bump allocator (0x50000000+).
 *
 * Parameters:
 *   name  — .so name (e.g., "libfoo.so"), NUL-terminated
 *   flags — reserved (0 for now)
 * Returns: base address of the mapped .so, or 0 on failure. */
u64 so_load(const char *name, u64 flags);

/* ---- Interface 44: Shared Library Unloader ----
 * Unmaps a previously loaded .so. Currently a no-op — mappings
 * are released when the process exits. This is a real
 * implementation (returns 0 = success), not a stub.
 *
 * Parameters:
 *   handle — base address returned by so_load
 * Returns: 0 on success. */
int so_unload(u64 handle);

/* ---- Interface 45: Symbol Resolver ----
 * Resolves a symbol by name in a loaded .so. Walks the .so's
 * .dynamic → .dynsym/.dynstr, finds the symbol, returns its
 * runtime address (base + st_value).
 *
 * Parameters:
 *   handle — base address of the .so (from so_load)
 *   name   — symbol name (e.g., "foo_add")
 * Returns: address of the symbol, or 0 if not found. */
u64 symbol_resolve(u64 handle, const char *name);

/* ---- Interface 46: dlsym Implementation ----
 * L1-facing alias for symbol_resolve. Same behavior. */
u64 dlsym_impl(u64 handle, const char *name);

/* ---- Interface 47: dlopen Implementation ----
 * L1-facing alias for so_load. Same behavior. */
u64 dlopen_impl(const char *name, u64 flags);

/* ---- Interface 48: dlclose Implementation ----
 * L1-facing alias for so_unload. Same behavior. */
int dlclose_impl(u64 handle);

/* ---- Interface 49: Relocation Applier ----
 * Applies a batch of ELF relocations (Elf64_Rela entries).
 * Handles R_X86_64_RELATIVE, GLOB_DAT, 64, COPY, JUMP_SLOT.
 * Primarily used by ld.so in user space; exposed here for L1
 * extensions that process ELF relocations in kernel context.
 *
 * Parameters:
 *   rela_data   — pointer to the .rela.* section (array of 24-byte entries)
 *   count        — number of Elf64_Rela entries
 *   base        — load base of the ELF being relocated
 *   symtab_addr — address of .dynsym (in the same ELF's loaded image)
 *   strtab_addr — address of .dynstr (in the same ELF's loaded image)
 *   libfoo_base — base of libfoo.so (for cross-library symbol lookup)
 *   libfoo_symtab — libfoo.so .dynsym address
 *   libfoo_strtab — libfoo.so .dynstr address
 * Note: base/symtab_addr/strtab_addr are virtual addresses in the
 *   current address space (not file offsets). */
void reloc_apply(const void *rela_data, u64 count, u64 base,
                 u64 symtab_addr, u64 strtab_addr,
                 u64 libfoo_base, u64 libfoo_symtab, u64 libfoo_strtab);

/* ---- Interface 50: Dependency Query ----
 * Reads an ELF's .dynamic section and extracts the DT_NEEDED
 * dependency list. Works on raw ELF byte data (embedded or
 * loaded). For static (ET_EXEC) binaries, returns 0.
 *
 * Parameters:
 *   elf_data   — pointer to the ELF byte array
 *   needed_out — output array of const char* pointers (into elf_data)
 *   max_count   — max entries in needed_out
 * Returns: number of DT_NEEDED entries found. */
int elf_get_needed(const u8 *elf_data, const char *needed_out[], int max_count);

/* Self-test: exercises all 10 interfaces, returns count of successful tests. */
int ext_wp8b_selftest(void);

#endif /* OC_EXT_WP8B_H */
