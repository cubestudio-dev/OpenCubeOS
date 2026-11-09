<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocbl — Open Cube OS Bootloader (WP-02-ocbl)

The handwritten NAK bootloader. It replaces GRUB on the boot path:
OCBL brings the machine up to 64-bit long mode, reads the NAK kernel
ELF from the boot medium, parses it with a shared ELF64 loader and
jumps to the kernel's 64-bit entry. **No GRUB code, component or
config is used anywhere in this tree.**

## Products

- `build/opencubenak-WP-02-ocbl.iso` — **THE user-facing deliverable**:
  one bootable ISO with dual El Torito entries, boots on BIOS and
  UEFI from the same medium (see below)
- `build/ocbl-bios.bin` — raw bootable disk image (BIOS/SeaBIOS path)
- `build/ocbl-uefi.efi` — native PE32+ EFI application (OVMF/UEFI path)

## BIOS path (16 → 32 → 64 bit)

```
LBA 0      stage 1  core_boot_bios_stage1.S  (512-byte MBR at 0x7C00)
LBA 1..31  stage 2a core_boot_bios_stage2.S  (0x7E00, 16/32-bit trampoline)
LBA 32..63 stage 2b core_boot_bios_stage2b.S + 64-bit C payload (0xBC00)
LBA 64..   kernel.elf, streamed to 0x20000 by stage 2a (INT 13h AH=42h)
```

stage 1 saves the boot drive, verifies INT 13h extensions (AH=41h)
and reads stages 2a+2b with ONE extended read (63 sectors, the
transfer buffer stays inside the first 64 KiB segment). stage 2a
reads the kernel in 64-sector batches (the sector count is patched
into the `ocbl_kernel_sectors` word of the image at build time),
opens A20, builds the identity page tables (first 1 GiB, 2 MiB
pages) and enters long mode. The 64-bit C payload brings up COM1 +
VGA, runs the shared ELF64 loader and jumps to the kernel's 64-bit
entry.

## UEFI path (PE32+)

`core_boot_uefi.c` is compiled by x86_64-w64-mingw32-gcc into a real
PE32+ image (`-Wl,--subsystem,10`). Hand-written UEFI definitions
(core_boot_uefi.h) give exact protocol layouts: the entry opens the
boot volume through Simple File System Protocol, reads
`\boot\kernel.elf`, reserves exactly the physical range the kernel
needs (probed via `core_boot_elf_probe`, then AllocatePages with
AllocateAddress), runs the shared ELF64 loader, stops the watchdog
and calls ExitBootServices with the standard retry loop. Before the
jump OCBL installs its own GDT so the kernel sees the exact segment
environment the BIOS path provides.

## Shared core (both paths)

- `boot/core_boot_elf_loader.c` — ELF64 validation, PT_LOAD mapping
  to p_paddr, `.ocbl_boot64` 64-bit-entry discovery
- `drivers/drv_serial_16550.c` — COM1, the core debug channel
- `lib/core_io_port.h` — shared port I/O helpers

Platform-specific drivers never mix: the BIOS path uses
`drivers/drv_video_vga.c` (VGA text), the UEFI path uses
`drivers/drv_video_uefi_gop.c` (GOP framebuffer takeover). A grep
gate in CI enforces the decoupling.

## The release ISO (dual El Torito)

`make -C ocbl iso` assembles `build/opencubenak-WP-02-ocbl.iso`
with xorriso (mkisofs compatibility mode — no external bootloader
involved). The same medium boots on both firmwares:

- **BIOS (SeaBIOS)**: El Torito entry 1 is the no-emulation image
  `boot/ocbl-bios-cd.bin`. The firmware loads the WHOLE image —
  stage 1 + stage 2a + stage 2b + the kernel.elf at image LBA 64 —
  contiguously at 0x7C00 and enters stage 1. This is the
  `OCBL_BOOT_CD` flavor of the same three stages: stage 1 makes no
  INT 13h call at all, stage 2a keeps `ocbl_kernel_sectors` at its
  link-time zero (the read loop is skipped) and the kernel is
  parsed straight from its in-memory address 0x7C00 + 32 KiB
  (0xFC00). The image layout is deliberately identical to the raw
  disk image, so both flavors stay in lockstep.
- **UEFI (OVMF/real firmware)**: El Torito entry 2 is `efi.img`, a
  1 MiB FAT volume (mtools) exposing `\EFI\BOOT\BOOTX64.EFI` and
  `\boot\kernel.elf`. The firmware mounts the El Torito volume
  and runs the exact same `ocbl-uefi.efi` code the FAT/USB path
  uses. `efi.img` is additionally appended as MBR partition 2
  (type 0xEF), so the ISO is USB-stick bootable on UEFI too.

`/boot/kernel.elf` is also visible as a plain ISO file — the very
  bytes both boot paths load. CI verifies the dual El Torito
catalog, the image paths and the kernel file contents.

## Kernel boot protocol (GRUB-less)

`e_entry` of a NAK kernel is the 32-bit Multiboot2 trampoline
(`_start`), meant for GRUB. OCBL hands over in long mode, so NAK
kernels publish the absolute address of their 64-bit entry
(`_start64`) in a `.ocbl_boot64` section; OCBL prefers it and falls
back to `e_entry`. Boot arguments are zeroed: the WP-01 kernel
ignores its arguments; a GRUB-less boot-parameter protocol is a
later work package.

## Build / run

```
make -C ocbl all           # ISO + bios + uefi, -Wall -Wextra -Werror
make -C ocbl iso           # the release ISO only
make -C ocbl run-bios      # SeaBIOS, raw disk, serial on stdio
make -C ocbl run-uefi      # OVMF (pflash) + QEMU virtual FAT ESP
make -C ocbl run-iso-bios  # SeaBIOS booting the ISO (-cdrom)
make -C ocbl run-iso-uefi  # OVMF (pflash) booting the ISO (-cdrom)
make -C ocbl test          # all four paths, serial logs to build/
make -C ocbl clean
```

Toolchain: gcc 14.2 (BIOS) + x86_64-w64-mingw32-gcc (UEFI PE32+),
both installable with `tools/install-toolchain.sh` (no root).
