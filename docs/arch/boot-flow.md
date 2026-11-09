<!-- SPDX-License-Identifier: Apache-2.0 -->
# Boot flow, instruction by instruction

## Stage 0 - firmware + GRUB

The ISO carries two boot images (legacy El Torito + EFI). Either way
GRUB 2.12 runs, shows the slot menu (ISO / A / B - the flag lives on
the target disk), and loads `boot/opencube.elf` with the multiboot2
protocol. GRUB also attaches the kernel ELF itself as a module with
cmdline `self` - the installer payload pattern.

## Stage 1 - boot.S (32 -> 64 bit)

`boot/boot.S` is the 32-bit entry: GRUB calls it in protected mode,
paging off, `eax=0x36d76289`, `ebx=mbi pointer`. It:

1. Sets up its own GDT.
2. Builds page tables identity-mapping the first 4 GiB with 2 MiB
   pages.
3. Enables PAE, LME, PGE.
4. Jumps to 64-bit long mode and calls `kmain`.

## Stage 2 - multiboot2 parse

`kernel/arch/x86_64/arch_multiboot2.c` walks the tag list. Tags are
packed types (all of them - the generic tag header, framebuffer, mmap,
meminfo, strings, modules, RSDP carriers). The parser is hostile-input
hardened (BUG-0142 fix): total_size bounds, per-tag minimums, string
NUL-termination, truncated-tag rejection - a malformed mbi degrades
gracefully instead of reading past the structure.

It extracts: framebuffer geometry, the physical memory map, the
command line, module list, and the ACPI RSDP (tags 14/15 - the ACPI
driver re-validates the signature at that address).

## Stage 3 - kmain bring-up

`kernel/main.c` in boot-log order: framebuffer console + 8x16 font,
IDT/GDT/TSS (256 gates, IST1 panic stack, IST2 debug stack), 8259
remap, PIT 100 Hz, PS/2 keyboard, COM1 RX into the keyboard queue,
line editor, `sti`, and a live exception self-test (3/3: #DE #UD #PF
caught, kernel alive) before anything else trusts the trap path.

Then: PMM (bitmap from the mmap), VMM, heap; driver domains; VFS with
the ramfs root; network; shell registration (233 commands); crash
recovery armed; `oc>` prompt.

## The two-stage link

The kernel links in two passes (`Makefile`): stage-1 links everything
except the GRUB boot images; `tools/embed_grub.py` then generates
`kernel/generated/grub_boot_data.c` embedding the 512-byte boot.img
plus a core.img built by `grub-mkimage` (the module set the on-disk
grub.cfg needs - biosdisk, part_msdos, fat, multiboot2, search,
serial console...); stage-2 links that in. This is how the in-system
`install`/`abdisk`/`grub-install` commands can write a bootable disk
without external files: the boot loader is inside the kernel.
