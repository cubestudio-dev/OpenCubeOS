<!-- SPDX-License-Identifier: Apache-2.0 -->
# The shared-object loader

`ocos/l1apps/ld_so.c` - the in-tree dynamic loader (a ring-3
program itself, linked at 0x10000000 via `ld_so.ld`), used by the
dynamic-linking user programs.

## What it handles

ELF dynamic objects: DT_NEEDED chains, symbol tables, relocation
processing (R_X86_64_RELATIVE / GLOB_DAT / JUMP_SLOT / 64
families), init arrays at load, and unload with reference
handling.

## The image modes

`tools/gen_userprogs.py` preserves the v2.x build modes per
program: ET_EXEC for the static apps, ET_DYN + `PT_INTERP=/lib/ld.so`
for `dyn_hello` / `pie_test` / `so_test` / `dlsym_test` /
`reloc_test` (the kernel rejects a silent ET_EXEC downgrade of a
PIE program - image type is part of the interface).

## The proof it works

The embedded fleet exercises every corner from ring 3:

```
ush> run dyn_hello           (loaded, init ran, hello printed)
ush> run so_test             (foo_add(2,3)=5 through the solib)
ush> run dlsym_test          (dlsym resolves foo_add(7,8)=15)
ush> run pie_test            (loaded at 0x... - PIE base assignment)
ush> run reloc_test          (foo_add=30 foo_global=42 fp=300)
```

plus `l1test`'s `ext_wp8b selftest 14/14` - the loader
self-check that rides every regression run. If your program loads
through this path, these are your regression anchors.

## Position independence

PIE objects relocate through the same relocation pass; the
loader's base assignment is what lets the `-fno-pie` apps and PIE
programs coexist in one registry - `pie_test`'s source shows the
exact relocation mix a dynamic program may carry.

## The libfoo leg

`libfoo.c` (the embedded solib) is built by
`tools/build_solib.py` into `solib_data.h` - the same embed chain
as every other program, so the staleness gate covers it.
