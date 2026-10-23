<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: the shared-object loader

`libs/ld_so.c` - a real dynamic loader for the kernel, used by L1
modules and the dynamic-linking user programs alike.

## The surface (l1/l1_wp8b.h)

```
elf_load_dynamic(elf_data, size, name)    -> load + relocate
ldso_run()                                 -> run loaded objects' init
so_load(name, flags) / so_unload(handle)
symbol_resolve(handle, name)
dlopen_impl(name, flags) / dlsym_impl(handle, name) / dlclose_impl(handle)
reloc_apply(rela, count, base, ...)
elf_get_needed(elf_data, size, ...)
```

## What it handles

ELF dynamic objects: DT_NEEDED chains (`elf_get_needed`), symbol
tables, relocation processing (`reloc_apply` - R_X86_64_RELATIVE/
GLOB_DAT/JUMP_SLOT/64 families), init arrays at `ldso_run`, and
unload with reference handling.

## The proof it works

The `userprogs/` fleet exercises every corner from ring 3:
`run dyn_hello`, `run so_test`, `run dlsym_test`, `run pie_test`,
`run reloc_test` - and `ext_wp8b_selftest()` self-checks the loader
in-kernel. If your module loads through this path, these are your
regression anchors.

## Position independence

PIE objects relocate through the same `reloc_apply`; the loader's
base assignment is what makes `-fno-pie` kernel code and PIE modules
coexist - see the `pie_test` source for the exact relocation mix a
module may carry.
