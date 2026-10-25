<!-- SPDX-License-Identifier: Apache-2.0 -->
<div align="center">

# Open Cube OS

**A from-scratch x86_64 kernel with a WinRE-style recovery shell.**

Current release: **v2.1.0** - the 17-issue fix release on top of the
2.0.0 ring-3 re-architecture: CI green again (and now a hard release
gate), the docs/interface truth per-entry (v1 233-command mapping
complete, the 278-declaration interface comparison, the declared
deletion register), regenerated command reference, website data
integrity (see
[Releases](https://github.com/cubestudio-dev/OpenCubeOS/releases)).

`oc>` is the prompt. Everything is extensible.

[Website](https://cubestudio-dev.github.io/OpenCubeOS/) ·
[Documentation](https://cubestudio-dev.github.io/OpenCubeOS/docs/) ·
[Downloads](#downloads) ·
[Building](BUILDING.md) ·
[Architecture](ARCHITECTURE.md) ·
[Interface Reference](INTERFACES.md)

</div>

---

## What it is

Open Cube OS is **L0 + L1**: a complete, self-contained 64-bit kernel
that boots on real hardware and in QEMU, launches its default ring-3
application (the ush user shell, **251 commands** across the L0
recovery shell and the L1 service surface), and falls back to the
recovery-shell prompt (`oc>`) when the ring-3 world cannot start.

- **Not a Linux fork.** 130,526 lines of original C/assembly under
  Apache 2.0 (verify: see *Stats* below).
- **Boots everywhere the PC platform goes:** hybrid BIOS + UEFI ISO,
  512 MiB RAM is comfortable, disk optional.
- **A real kernel:** preemptive scheduler, 4-level paging, ring-3 user
  programs with 54 syscalls, VFS over FAT32/exFAT/ext4/ramfs, a full
  TCP/IP stack with TLS 1.2/1.3 and SSH (client *and* server), 16
  driver families from NVMe to xHCI to ACPI AML, crash dumps, hardware
  watchdog (Intel TCO / ACPI WDAT), OTA A/B updates.
- **A recovery shell by design:** the system *is* the WinRE-style
  environment - boot, diagnose, repair, install, update.

The two-layer architecture:

```
L0  Open Cube OS        kernel + oc> recovery shell     <- this repo
L1  ring-3 services     17 SYS_L1SVC categories / 158 ops + ush
```

2.0.0 ships the L1 side in-tree: `l1apps/` holds the ring-3
applications (ush - the default boot entry - plus ld.so and the
dynamic-linking test programs), and every operational command class
moved behind the SYS_L1SVC service surface
([INTERFACES.md](INTERFACES.md) - the v1->v2 mapping table is the
compatibility contract).

## Highlights

| Area | What you get |
|------|--------------|
| Boot | GRUB 2.12 hybrid ISO (BIOS El Torito + UEFI image), multiboot2 with the kernel attached as a `self` module, A/B slot menu |
| Memory | PMM bitmap over the multiboot2 map, 4-level VMM, kmalloc heap with real growth via #PF |
| CPU | Preemptive priority + round-robin scheduler, 256-gate IDT with IST stacks, 100 Hz PIT + HPET/PM-timer/TSC sources, LAPIC |
| Ring 3 | ELF loader, 54 syscalls, signals, fork/exec/pipes/select/mmap, dynamic linking (in-tree ld.so, PIE, dlsym); SYS_L1SVC + SYS_NETCMD service bridges |
| Storage | AHCI, ATA/ATA-DMA, NVMe, virtio-blk, USB MSC, SCSI (LSI); FAT32 with LFN, exFAT, ext4, ramfs; GPT/MBR partitions; A/B slots |
| Network | e1000/e1000e/igb/ixgbe/rtl8139/rtl8169-family/BCM57xx drivers, ARP/IPv4/ICMP/UDP/DNS/TCP with congestion control, sockets, firewall, HTTPS client (TLS 1.2/1.3 + X.509 chains), SSH client + sshd |
| USB | UHCI, OHCI, EHCI, xHCI; mass storage, HID, serial, audio classes |
| Media | HDA / AC97 / SB16 / ES1370 / virtio sound; UVC cameras with MJPEG |
| Platform | ACPI (FADT/DSDT/MCFG, real AML walker, S3 suspend/resume), Intel TCO + ACPI WDAT hardware watchdogs, HPET, i2c/SMBus |
| Shell | ush (ring 3, default boot entry): 133 service commands + built-ins with pipes/redirection/wildcards/env/aliases/jobs; oc> recovery shell: 99 commands |
| TUI | Desktop, taskbar, menus, forms, editor, system apps - plus a Chinese console layer (16x16 font, pinyin IME) |
| Reliability | Crash dumps to serial + /etc crashlog, kernel log with rotation + levels + filters, health monitor (CPU/mem/tasks/IO + JSON export), OTA with SHA256 + signatures and rollback |

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
ush> exit          # -> drops into the oc> recovery shell (99 commands)
```

UEFI instead of BIOS: `make run-uefi`. Windows hosts: `build.ps1`
(WSL2/MSYS2). Full guides: [BUILDING.md](BUILDING.md).

## Downloads

Every release ships three assets from the same byte-reproducible
pipeline - the current release is always on the
[GitHub releases page](https://github.com/cubestudio-dev/OpenCubeOS/releases)
and mirrored on the
[website downloads page](https://cubestudio-dev.github.io/OpenCubeOS/):

| Asset | What it is |
|-------|------------|
| `opencube-2.1.0.iso` | Hybrid BIOS+UEFI bootable ISO - boot it in QEMU or write to USB |
| `opencube-2.1.0-src.zip` | Deterministic source zip (`git archive`) |
| `opencube-2.1.0-update.tar.gz` | OTA update package (SHA256 + signature verified, A/B slots) |

Each release note carries the SHA256 of all three; the website,
the Release and the private mirror hold byte-identical copies.

## Documentation

- [BUILDING.md](BUILDING.md) - build on Linux/Windows, QEMU,
  cross-compilation (ARM64/RISC-V shells), reproducible verification
- [ARCHITECTURE.md](ARCHITECTURE.md) - the L0 architecture, top to bottom
- [INTERFACES.md](INTERFACES.md) - the 251-command surface by home, the v1->v2 mapping and the L1SVC service tables
  (generated from the tree: `make api-docs`)
- [FAQ.md](FAQ.md) - common questions
- [CONTRIBUTING.md](CONTRIBUTING.md) - ground rules and the PR bar
- The full documentation center (user manual, developer handbook,
  driver bring-up guides, L1 extension guide, internals):
  **https://cubestudio-dev.github.io/OpenCubeOS/docs/**

## Stats

All numbers below are machine-verified at build time
(`make api-docs` writes `docs/data/*.json`; the README verification
command is quoted per item):

- **Source code**: 130,526 lines across 379 files - core + arch +
  drivers + services + recovery + tui + chinese + boot + l1apps,
  headers included, docs/tools/tests and generated data blobs
  excluded.  Verify:
  `git ls-files | grep -E '\.(c|h|S)$' | grep -E '^(core|arch|drivers|services|recovery|tui|chinese|boot|l1apps)/' | grep -v '^core/generated/' | grep -vE 'userprogs_data|chinese_font_data|pinyin_data|grub_boot_data' | xargs wc -l`
- **Commands**: 251 total - 99 in the L0 recovery shell (`help -a`
  at the `oc>` prompt) + 133 ring-3 SYS_L1SVC service commands +
  19 ush built-ins. Every 1.0.0 command is mapped - see the
  v1->v2 table in [INTERFACES.md](INTERFACES.md).
- **L1SVC service surface**: 17 categories / 158 numbered ops -
  see [INTERFACES.md](INTERFACES.md) for the exact tables.
- **Syscalls**: 54 - verify:
  `grep -c '#define SYS_' core/core_syscall.h`
- **CI**: green on every push - build, ISO, packages, host tests and
  the reproducibility gate
  (`.github/workflows/build.yml`).

## License

Apache 2.0 - see [LICENSE](LICENSE). Every source file carries
`SPDX-License-Identifier: Apache-2.0`.

Copyright 2026 cubestudio-dev &lt;cubestudio@qq.com&gt;
