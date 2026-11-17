<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCBL - the Open Cube OS boot loader: architecture and responsibilities

OCBL is the native boot loader of Open Cube OS.  It replaces GRUB on
Open Cube OS media: the boot code `ocbl-install` writes, the recovery
disk's stage1, and the ISO's boot image are all OCBL.  It lives in
`ocbl/` and builds in **two firmware flavors** from one shared core.

## One core, two firmware flavors

| Flavor | Entry | Real-mode plumbing | Console | Disk | Video |
|---|---|---|---|---|---|
| BIOS | `bios/stage1.asm` | 16-bit trampolines (`tramp16.asm`), int13 DAP reads through a sub-1 MiB bounce window | VGA text + serial | int13 | VBE (`ocbl_vbe.c`) |
| UEFI | `efi/efi_entry.c` | none - native 64-bit from the start | GOP console + serial | EFI BlockIo (**drive numbering starts at zero**) | GOP (`ocbl_gop_efi.c`) |

Both flavors compile the same `common/` core: the entry scanner
(`ocbl_scan.c`), the menu (`ocbl_menu.c`), the filesystems
(`ocbl_fs.c`), the partition decoder (`ocbl_part.c`), the ELF loader
(`ocbl_load.c`), the boot-state monitor (`ocbl_statemon.c`) and the
watchdog driver (`ocbl_wdt.c`).

## The handoff contract

OCBL loads the kernel and hands off exactly like GRUB does - 32-bit
protected mode, paging off, `EAX = 0x36D76289` (the multiboot2 magic),
`EBX` = the MBI physical address, jump to the kernel's 32-bit entry.
The fixed physical layout of a BIOS-flavor load:

| Address | Contents |
|---|---|
| `0x30000`-`0x3FFFF` | 64 KiB read bounce window (int13 DAP must target <1 MiB) |
| `0x50000` | multiboot2 info (64 KiB) |
| `0x400000+` | kernel PT_LOAD segments (kernel link base) |
| `0x800000+` | the mb2 "self" module copy of the kernel ELF |
| `0x1000000+` | the OCIRE payload (menu entry OCBL_ENT_OCIRE) |

The UEFI flavor keeps the same MBI contract but exits boot services
instead of downgrading to real mode.

## What OCBL scans for

The scanner walks every BIOS drive plus the boot CD, decodes the
partition tables, mounts the filesystems it knows and produces the
entry table the menu renders.  Seven entry classes:

| Class | Detected by |
|---|---|
| OCOS | any volume with `/boot/opencube.elf` carrying the multiboot2 magic; the A/B flag protocol picks the slot |
| OCIRE | the 0xEE shadow volume (or any volume) with `/boot/ocire.elf`; on the WP-07 recovery disk, the two independent slot partitions |
| OCSE | an ISO9660 volume carrying `/ocse.marker` (the install-media contract) |
| WINDOWS | a volume with `/EFI/Microsoft/Boot/bootmgfw.efi` |
| LINUX | an ESP with a foreign EFI loader (`grubx64.efi` / `shimx64.efi` / `systemd-bootx64.efi` under `/EFI/<vendor>/`) - the vendor dir names the entry |
| GENERIC | an ESP with `/EFI/BOOT/BOOTX64.EFI` matching none of the above |
| SHUTDOWN | synthesized, always last |

Declared limit: 3.0.0 ships no ext2/3/4 driver in OCBL, so Linux
systems are detected through their ESP loaders, never through
`/boot` on an ext root; Windows likewise (NTFS is not parsed -
detection is ESP-based, booting is chainload-based).

## Responsibilities beyond loading

OCBL is also the **first responder** of the reliability chain:

- it arms the boot-phase watchdog budget before every monitored
  payload handoff (the i6300ESB relay),
- it diagnoses the previous boot from the state areas after a reset,
- it records boot failures, counts consecutive failures and routes
  the machine into OCIRE when the threshold is reached.

Those mechanics are the subject of
[dual-firmware boot and A/B slot decisions](ocbl-ab.md) and
[troubleshooting and status codes](ocbl-troubleshooting.md).
