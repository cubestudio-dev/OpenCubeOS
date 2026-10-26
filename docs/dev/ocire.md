<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCIRE - OpenCubeOS Independent Recovery Environment

Status: **3.0.0-WP-01-ocire-core** (this work package lands the core:
OCIRE boots independently and runs a minimal shell).

## What OCIRE is

OCIRE is a second L0 kernel, fully independent of the Open Cube OS
(OCOS) kernel.  It lives in its own source tree (`ocire/`), shares no
headers with OCOS, links with its own linker script
(`ocire/linker_ocire.ld`) and produces its own boot image
(`build/ocire/ocire.iso`).  The two-kernel architecture:

- **L0 = provides services** — OCOS stays the full-service kernel.
  OCIRE is an L0 *peer*: a recovery kernel that boots when OCOS
  cannot.
- **Independent** — OCIRE must be able to boot, run and power-cycle
  the machine without any OCOS code, data or partition being
  reachable or required.

## What WP-01 ships

| Area | WP-01 (this) | WP-02 | WP-03 |
|---|---|---|---|
| Boot + minimal shell | **here** | — | — |
| help / shutdown / reboot | **here** | — | — |
| Recovery features, FAT12, self-check, repair | — | here | — |
| Boot-failure detection, 1-minute timeout | — | here | — |
| OCOS <-> OCIRE interface, process protection | — | — | here |

The shell command set is exactly three commands (`help`, `shutdown`,
`reboot`) — fixed for this work package, nothing more.

## Source layout (ocire/)

```
ocire/
  ocire_mb2_header.S    multiboot2 header (80x25 text mode request)
  ocire_boot.S          32-bit GRUB entry -> GDT + 4 GiB identity map
                        -> long mode -> ocire_main; exception stubs
  ocire_main.c          kernel entry: console, IDT, ACPI, shell
  ocire_console.c/.h    dual console: COM1 115200 + VGA text 0xB8000
  ocire_input.c/.h      serial + PS/2 polled input (shift/caps/backsp)
  ocire_idt.c/.h        minimal IDT: 32 exception vectors, faults
                        print a diagnostic instead of triple-faulting
  ocire_acpi.c/.h       RSDP/XSDT/FADT (checksums verified), tiny \_S5
                        package reader, PM1a S5 + RESET_REG + 8042
  ocire_shell.c/.h      the three commands, honest unknown-command hint
  ocire_util.c/.h       freestanding mem/string helpers
  ocire_io.h            port I/O inline helpers
  ocire_types.h         base scalar types
  ocire.mk              build targets (included from the top Makefile)
  linker_ocire.ld       own link layout (1 MiB load address)
  grub-ocire.cfg        standalone ISO menu (single entry)
  grub-dual.cfg         dual-boot disk menu (OCOS default, OCIRE entry)
```

Build targets: `make ocire-kernel`, `make ocire-iso`, `make
ocire-disk`, `make ocire-test`, `make ocire-dual-test` (run targets
`ocire-run-bios/uefi[-gui]` are also available).  Compilation uses
the same `-Wall -Wextra -Werror` zero-warning gate as the OCOS
kernel; CI runs the OCIRE build on every push.

## How the two OSes coexist (WP-01 layout)

```
hd0 (ocire-boot.img, built by tools/make_ocire_disk.sh)
  MBR + GRUB (BIOS: boot.img + core.img in the post-MBR gap;
              UEFI: /EFI/BOOT/BOOTX64.EFI on the FAT32 partition)
  p1 FAT32: /boot/opencube.elf (OCOS kernel)
            /boot/ocire.elf    (copy of the OCIRE kernel)
            /boot/grub/grub.cfg (the dual menu)
  -> GRUB menu entry 1 boots OCOS (default)
  -> GRUB menu entry 2 boots OCIRE

hd1 (ocire-hd.img) - the OCIRE disk
  MBR with ONE entry, type 0xEE ("GPT protective shadow"), no GPT
  header behind it; the raw OCIRE kernel image is stored at the
  partition start (LBA 2048).
  -> the GRUB menu loads it with a whole-disk blocklist:
     multiboot2 (hd1)2048+128
  -> OCOS refuses the entire partition table (0xEE makes its parser
     switch to GPT mode, which fails without a GPT header), so the
     OCIRE partition has no device node and no filesystem path in
     OCOS - see "Partition isolation" below.
```

Two lessons from bring-up, encoded here for the record:

1. GRUB 2.12's msdos partmap **skips** 0xEE entries — a partition path
   like `(hd1,msdos1)` does not exist for GRUB even though the same
   entry is perfectly valid on disk.  The menu therefore uses a
   whole-disk blocklist.
2. GRUB menus have no digit hotkeys; automation selects entries with
   the ANSI cursor-down sequence over the serial menu terminal.

## Partition isolation (the WP-01 acceptance test)

Measured in `evidence/3_0_0_wp01_ocire/isolation.log` from inside a
running OCOS with both disks attached:

- `partscan: 1 partition device(s) registered` — the OCIRE disk
  contributes **zero** child devices (only the dual-boot disk's p1
  registers).
- `parted hda` (OCIRE disk) → `no partition table found`.
- `mkfs.fat32 hdap1 confirm` → `device not found` — there is no node
  to write through.
- `disk_rw_test hdap1` → `device lookup FAIL` — there is no node to
  read through.
- `df` — only OCOS volumes are mounted; nothing from the OCIRE disk.

Honest boundary (also measured, `isolation_rawdecl.log`): whole-disk
device nodes still exist — `disk_rw_test hda` and `mkfs.fat32 hda
confirm` both work on a throwaway copy, exactly like Linux root with
`/dev/sdb`.  Blocking administrator-level whole-disk operations
requires an OCOS block-layer policy — an OCOS kernel change this
work package explicitly forbids — and belongs to WP-03 (process
protection).

## Tests

- `tools/run_ocire_tests.py` (`make ocire-test`) — 14 cases, BIOS +
  UEFI: boot to the shell prompt, `help`, unknown-command hint,
  backspace editing, `shutdown` (ACPI S5; QEMU really exits) and
  `reboot` (8042/RESET_REG; with `-no-reboot` QEMU really exits).
- `tools/run_ocire_dual_tests.py` (`make ocire-dual-test`) — 11
  cases: the four dual-boot combinations (BIOS/UEFI x menu default
  OCOS / menu-pick OCIRE), the five isolation probes and the two
  whole-disk declaration probes.
- Full OCOS regression matrix on a tree that contains OCIRE but zero
  OCOS edits: `evidence/3_0_0_wp01_ocire/regression/` (host tests,
  reproducible-check, wp12, wp10_tui, chinese, tuiqa x2, native x2,
  l1_per_app, wait4 fresh-boot battery 20/20, boot gates) — all green
  and identical to the 2.2.0 release baseline.
