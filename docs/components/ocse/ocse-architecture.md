<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCSE - OpenCubeOS Setup Environment: architecture and responsibilities

OCSE is the operating-system **installer environment** of Open Cube OS:
a small, freestanding kernel that boots from the install media and
turns a blank machine into a bootable Open Cube OS system.  It lives
in its own source tree (`ocse/`), links with its own linker script
(`ocse/linker_ocse.ld`) and shares no code with the running system
(OCOS) or the recovery environment (OCIRE).

## Why a dedicated installer kernel

The install must succeed on a machine with **nothing on it** - no
operating system, no drivers, no shell.  OCSE therefore carries its
own minimal versions of everything it needs:

| Module | Files | Responsibility |
|---|---|---|
| Kernel core | `kernel/`, `boot/` | freestanding boot, scheduling of the GUI/input loop |
| Screen | `screen_fb` | framebuffer console + the GUI widget layer |
| Input | keyboard driver | menu keys, text fields, hotkeys |
| Disk access | `ocse_disk.c`, `ocse_atapi.c` | ATA/ATAPI and BIOS/UEFI disk I/O |
| Partitioning | `ocse_part.c`, `ocse_detect.c` | MBR/GPT decode, free-gap detection, existing-system detection |
| Filesystems | `fs/`, `ocse_mkfs.c`, `ocse_ext4w.c` | FAT32/exFAT/ext4 create and read/write |
| Install payload | `ocse_iso.c`, `ocse_inst.c` | ISO9660 read, the install state machine |
| Integrity | `ocse_sha256.c` | SHA-256 digests (media check, password hashing) |
| User interface | `ocse_gui.c`, `ocse_wizard.c`, `ocse_cl.c` | the panel, the 9-step wizard, the OCSECL shell |
| Localization | `ocse_i18n.c` | English / Simplified Chinese, live switching |

## The safety model

OCSE is deliberately **incapable of damaging an existing Open Cube OS
installation by accident**:

- It **refuses** a target that already boots `opencube.elf` (the
  existing-system check) - the wizard shows the refusal instead of a
  partition list.
- Partitioning uses **the largest free gap** on the target disk; it
  never relocates or deletes foreign partitions to make room.
- Formatting applies **only to the partition OCSE itself created** in
  the same wizard session.
- Every destructive OCSECL command (`mkpart`, `rmpart`, `mkfs.*`,
  `install`, `ocbl-install`, `dd`) requires a literal `confirm`
  word as its last argument.

## Memory discipline: the .ocsework zone

All mutable installer state lives in a dedicated `.ocsework` NOLOAD
section - the rest of the image is pure code and const data.  The
environment is freestanding (no libc, no printf): every text buffer is
built with bounded string helpers, and a line that cannot fit is
refused with a clear error rather than overflowing (the discipline
introduced by the buffer-overflow fix in the wizard's command
builder).

## Boot and detection

The install media is an ISO9660 volume that carries a one-byte
`/ocse.marker` file - the WP-06 install-media contract.  The OCBL
boot loader recognizes the marker and lists the media as an OCSE
entry in its boot menu.  On boot, OCSE runs four hardware checks
(`hwcheck`) and then starts the graphical panel; the OCSECL shell is
one hotkey away at any time.

## Relationship to the other components

| Component | Role | See |
|---|---|---|
| OCBL | the boot loader OCSE installs (`ocbl-install`) | [OCBL overview](../ocbl/index.md) |
| OCOS | the system OCSE installs onto the target | the main manuals |
| OCIRE | the recovery environment, installed on the A/B recovery disk | the developer handbook |

Next: [the 9-step wizard and the OCSECL command dictionary](ocse-wizard.md).
