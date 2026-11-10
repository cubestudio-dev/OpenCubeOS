<!-- GENERATED mirror of the repository-root README.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
<div align="center">

# Open Cube OS

**A from-scratch x86_64 kernel that boots into ring 3.**

Current development preview: **3.0.0-WP06g3** - the OCBL bootloader, a
pure L0 service kernel (no shell, no TUI, no IME in L0), 275 ring-3
programs under `src/L1/`, and the OCSE install environment with the
9-step GUI install wizard (every wizard mutation is a real OCSECL
command, en/zh). Stable line: **2.2.0** (the last formal release of
the 2.x world, kept for reference and real-hardware stability). See
[Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases).

`ush>` is the prompt. Everything is extensible.

[Website](https://ocos.cubestudio.cn/) ·
[Documentation](https://ocos.cubestudio.cn/docs/) ·
[Downloads](#downloads) ·
[Building](BUILDING.md) ·
[Architecture](ARCHITECTURE.md) ·
[Interface Reference](INTERFACES.md)

</div>

---

## What it is

Open Cube OS is **L0 + L1**:

- **L0 provides services.** A pure service kernel - the boot chain,
  the core mechanisms, the drivers, the root services and the numbered
  interface. 3.0.0-WP03 deleted the L0 user plane entirely (the 2.x
  `oc>` recovery shell, the in-kernel TUI, the input method engine);
  L0 finishes boot at `L0 ready, waiting for L1` and hands the machine
  to the default ring-3 entry.
- **L1 uses services.** 275 ring-3 programs under `src/L1/` (242 app
  folders + 8 flat `.c` + 25 flat `.asm`): the default boot entry is
  the `ush` user shell (**285 commands = 274 registry apps + 11
  built-ins, zero commands in L0**), plus the TUI desktop, the editor,
  the Chinese console + pinyin IME and one command per app.

- **Not a Linux fork.** 128,890 lines of original C/assembly under
  Apache 2.0 (verify: see *Stats* below).
- **Boots everywhere the PC platform goes:** one hybrid BIOS + UEFI
  ISO through OCBL, the in-tree bootloader (512-byte real-mode stage 1
  / EFI entry, 16→32→64-bit long mode, dynamic disk scan) - 512 MiB
  RAM is comfortable, disk optional.
- **A real kernel:** preemptive scheduler, 4-level paging, ring-3 user
  programs with 54 syscalls, VFS over FAT32/exFAT/ext4/ramfs, a full
  TCP/IP stack with TLS 1.2/1.3 and SSH (client *and* server), 15
  driver families from NVMe to xHCI to ACPI AML, crash dumps, hardware
  watchdog (Intel TCO / ACPI WDAT), OTA A/B updates.
- **Honest by design:** if the ring-3 world cannot start, L0 says so
  and halts - the guidance is a reboot or the OCBL boot menu (there is
  deliberately no L0 shell to fall back to).

The two-layer architecture:

```
L0  OCOS (src/L0/)     kernel + drivers + root services + the
                       numbered L1SVC ABI - no shell, no TUI
L1  the world (src/L1/)  275 programs: ush, TUI, editor, IME,
                       one command per app (285 commands)
```

## Highlights

| Area | What you get |
|------|--------------|
| Boot | OCBL - the in-tree bootloader: hybrid BIOS + UEFI ISO, 16→32→64-bit long mode, dynamic disk scan, boot menu with a 10 s countdown (no GRUB in the 3.0.0 chain) |
| Memory | PMM bitmap over the boot memory map, 4-level VMM, kmalloc heap with real growth via #PF |
| CPU | Preemptive priority + round-robin scheduler, 256-gate IDT with IST stacks, 100 Hz PIT + HPET/PM-timer/TSC sources, LAPIC |
| Ring 3 | ELF loader, 54 syscalls, signals, fork/exec/pipes/select/mmap, dynamic linking (in-tree ld.so, PIE, dlsym); SYS_L1SVC + SYS_NETCMD service bridges |
| Storage | AHCI, ATA/ATA-DMA, NVMe, virtio-blk, USB MSC, SCSI (LSI); FAT32 with LFN, exFAT, ext4, ramfs; GPT/MBR partitions; A/B slots |
| Network | e1000/e1000e/igb/ixgbe/rtl8139/rtl8169-family/BCM57xx drivers, ARP/IPv4/ICMP/UDP/DNS/TCP with congestion control, sockets, firewall, HTTPS client (TLS 1.2/1.3 + X.509 chains), SSH client + sshd |
| USB | UHCI, OHCI, EHCI, xHCI; mass storage, HID, serial, audio classes |
| Media | HDA / AC97 / SB16 / ES1370 / virtio sound; UVC cameras with MJPEG |
| Platform | ACPI (FADT/DSDT/MCFG, real AML walker, S3 suspend/resume), Intel TCO + ACPI WDAT hardware watchdogs, HPET, i2c/SMBus |
| Shell | ush (ring 3, the default boot entry): 274 registry apps + 11 built-ins with pipes/redirection/wildcards/env/aliases/jobs; zero commands in L0 (the 2.x recovery shell was removed in WP-03) |
| TUI | Desktop, taskbar, menus, forms, editor, system apps - ring-3 programs under src/L1/ riding the L1SVC tui service, plus a Chinese console layer (16x16 font, pinyin IME) |
| Reliability | Crash dumps to serial + /etc crashlog, kernel log with rotation + levels + filters, health monitor (CPU/mem/tasks/IO + JSON export), OTA with SHA256 + signatures and rollback |
| OCBL | The in-tree bootloader (`ocbl/`): BIOS stage 1 (El Torito / MBR) + UEFI entry, the boot menu, the installer target (`ocbl-install`) |
| OCSE | The install-environment kernel (`src/L0` sibling, own build): boots standalone, runs the 9-step GUI install wizard + OCSECL - every mutation a real command |
| OCIRE | The independent recovery kernel (`ocire/`): a second L0 that shares no code with OCOS, boots standalone from its own ISO, stores its image in an isolated partition OCOS cannot read or write (see `docs/dev/ocire.md`) |

## Quick start

```
# build (Linux): needs gcc nasm xorriso mtools grub-common
#               grub-pc-bin grub-efi-amd64-bin zip python3
./build.sh iso

# run it
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1

# inside the guest (the default boot entry is the ring-3 ush shell)
ush> help
ush> dhcp
ush> ping 10.0.2.2
ush> wget 10.0.2.2
ush> uname -a
```

UEFI instead of BIOS: `make run-uefi`. Windows hosts: `build.ps1`
(WSL2/MSYS2). Full guides: [BUILDING.md](BUILDING.md).

## Downloads

Two channels ship from the same byte-reproducible pipeline - both are
on the [GitHub releases page](https://github.com/cubestudio-dev/OpenCubeOS/releases)
and mirrored on the [website downloads page](https://ocos.cubestudio.cn/download.html):

| Channel | Assets |
|---------|--------|
| **3.0.0-WP06g3** (development preview) | `opencube-3.0.0-WP06g3.iso` (boots OCBL → OCOS → ush) · `ocse-3.0.0-WP06g3.iso` (the GUI install environment, en/zh) |
| **2.2.0** (stable, the 2.x line) | `opencube-2.2.0.iso` · `opencube-2.2.0-src.zip` (deterministic source) · `opencube-2.2.0-update.tar.gz` (OTA) |

Each release note carries the SHA256 of every asset; the website,
the Release and the private mirror hold byte-identical copies.

## Documentation

- [BUILDING.md](BUILDING.md) - build on Linux/Windows, QEMU,
  cross-compilation (ARM64/RISC-V shells), reproducible verification
- [ARCHITECTURE.md](ARCHITECTURE.md) - the L0/L1 architecture, top to bottom
- [INTERFACES.md](INTERFACES.md) - the 285-command surface by home, the v1→v3 mapping and the L1SVC service tables
  (generated from the tree: `make api-docs`)
- [FAQ.md](FAQ.md) - common questions
- [CONTRIBUTING.md](CONTRIBUTING.md) - ground rules and the PR bar
- The full documentation center (user manual, developer handbook,
  driver bring-up guides, L1 extension guide, internals):
  **https://ocos.cubestudio.cn/docs/**

## Stats

All numbers below are machine-verified at build time
(`make api-docs` writes `docs/data/*.json`; the README verification
command is quoted per item):

- **Source code**: 128,890 lines across 624 files - src/L0/ (boot,
  core, drivers, services) + src/L1/, headers included, generated
  data blobs excluded (the generator caliber: `tools/gen_api_docs.py`
  `STAT_DIRS`/`STAT_EXCLUDE`).  Verify:
  `find src/L0/core src/L0/drivers src/L0/services src/L0/boot src/L1 \( -name '*.c' -o -name '*.h' -o -name '*.S' \) | grep -v generated | grep -vE 'chinese_font_data|pinyin_data|grub_boot_data|userprogs_data' | xargs wc -l | tail -1`
- **Commands**: 285 total - 274 ring-3 registry apps (one command =
  one app = one folder under `src/L1/`) + 11 ush built-ins + 0 in L0.
  Every 1.0.0 command is mapped - see the v1→v3 table in
  [INTERFACES.md](INTERFACES.md).
- **L1SVC service surface**: 20 categories / 190 numbered entries -
  see [INTERFACES.md](INTERFACES.md) for the exact tables.
- **Syscalls**: 54 - verify:
  `grep -c '#define SYS_' src/L0/services/sysapi/core_syscall.h`
- **CI**: green on every push - build, ISO, packages, host tests and
  the reproducibility gate
  (`.github/workflows/build.yml`).

## License

Apache 2.0 - see [LICENSE](LICENSE.md). Every source file carries
`SPDX-License-Identifier: Apache-2.0`.

Copyright 2026 cubestudio-dev &lt;cubestudio@qq.com&gt;
