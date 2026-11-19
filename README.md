<!-- SPDX-License-Identifier: Apache-2.0 -->
<div align="center">

# Open Cube OS

**A from-scratch x86_64 kernel that boots into ring 3.**

Current release: **3.0.0-WP08-driver-framework** - the OCBL
bootloader, a pure L0 service kernel (no shell, no TUI, no IME in
L0) with the driver framework (device tree, power hooks, hotplug,
failure isolation, deterministic init order), **300 ring-3
programs** under `ocos/l1apps/` - now organized
around **8 merged mega-apps** (`update` / `disk-tool` / `net-tool` /
`sound-tool` / `files` / `sysdiag` / `hwinfo` / `power`), each an
intent wizard *and* a verbatim dispatch for every classic command
underneath. Stable line: **2.2.0** (the last formal release of the
2.x world, kept for reference and real-hardware stability). See
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
  the core mechanisms, the drivers, the root services and the
  numbered interface. 3.0.0-WP03 deleted the L0 user plane entirely
  (the 2.x `oc>` recovery shell, the in-kernel TUI, the input
  method engine); L0 finishes boot at `L0 ready, waiting for L1`
  and hands the machine to the default ring-3 entry.
- **L1 uses services.** 300 ring-3 programs under `ocos/l1apps/`
  (one command = one app = one folder): the default boot entry is
  the `ush` user shell (**311 commands = 300 registry apps + 11
  built-ins, zero commands in L0**), the TUI desktop, the editor,
  the Chinese console + pinyin IME - and the 8 merged mega-apps
  that give the whole surface an intent-based front door:
  no-argument wizards with auto-detection and cross-domain repair
  (the `update` wizard fixes a missing IP through DHCP and a
  missing A/B disk through `abdisk` before it ever checks for
  updates), `direct` explicit passthrough, and classic verbatim
  dispatch with zero functionality loss.

- **Not a Linux fork.** 163,935 lines of original C/assembly across
  807 files under Apache 2.0 (verify: see *Stats* below).
- **Boots everywhere the PC platform goes:** one hybrid BIOS + UEFI
  ISO through OCBL, the in-tree bootloader (512-byte real-mode
  stage 1 / EFI entry, 16→32→64-bit long mode, dynamic disk scan) -
  512 MiB RAM is comfortable, disk optional.
- **A real kernel:** preemptive scheduler, 4-level paging, ring-3
  user programs with 54 syscalls, VFS over FAT32/exFAT/ext4/ramfs,
  a full TCP/IP stack with TLS 1.2/1.3 and SSH (client *and*
  server), 38 drivers across 7 declared families plus the driver
  framework (device tree, power hooks, hotplug, failure isolation),
  crash
  dumps, hardware watchdog (Intel TCO / ACPI WDAT), OTA A/B updates,
  and the WP07a boot-state monitor: six boot stages recorded into a
  fixed state area, per-phase watchdog budgets, and a post-mortem
  BOOT_FAILED record OCIRE reads after the reboot to print the
  stall point and repair advice.
- **Honest by design:** if the ring-3 world cannot start, L0 says so
  and halts - the guidance is a reboot or the OCBL boot menu (there is
  deliberately no L0 shell to fall back to).

The two-layer architecture:

```
L0  OCOS (ocos/)          kernel + drivers + root services + the
                          numbered L1SVC ABI - no shell, no TUI
L1  the world (ocos/l1apps/)  300 programs: ush, TUI, editor, IME,
                          one command per app, and the 8 merged
                          mega-apps (311 commands total)
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
| Shell | ush (ring 3, the default boot entry): 300 registry apps + 11 built-ins with pipes/redirection/env/aliases/jobs; zero commands in L0 |
| Mega-apps | The 8 merged apps (`update`, `disk-tool`, `net-tool`, `sound-tool`, `files`, `sysdiag`, `hwinfo`, `power`): intent wizards with cross-domain repair + `direct` passthrough + classic verbatim dispatch; destructive paths guarded by retype + y/N double confirmation |
| TUI | Desktop, taskbar, menus, forms, editor, system apps - ring-3 programs riding the L1SVC tui service, plus a Chinese console layer (16x16 font, pinyin IME) |
| Reliability | Crash dumps to serial + /etc crashlog, kernel log with rotation + levels + filters, health monitor (CPU/mem/tasks/IO + JSON export), OTA with SHA256 + signatures and rollback |
| OCBL | The in-tree bootloader (`ocbl/`): BIOS stage 1 (El Torito / MBR) + UEFI entry, the boot menu, the installer target (`ocbl-install`) |
| OCIRE | The independent recovery kernel (`ocire/`): a second L0 that shares no code with OCOS, boots standalone from its own ISO, stores its image in an isolated partition OCOS cannot read or write (see `docs/dev/ocire.md`) |

## Quick start

```
# build (Linux): needs gcc nasm xorriso mtools grub-common
#               grub-pc-bin grub-efi-amd64-bin zip python3
tools/build.sh iso

# run it
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1

# inside the guest (the default boot entry is the ring-3 ush shell)
ush> hwinfo            (wizard: what hardware is this machine?)
ush> update            (wizard: network -> A/B -> DNS -> check -> install)
ush> help -a           (all 300 registry programs)
ush> dhcp
ush> ping 10.0.2.2
ush> lsblk             (or: disk-tool, and pick it from the menu)
```

UEFI instead of BIOS: `make run-uefi`. Windows hosts: `build.ps1`
(WSL2/MSYS2). Full guides: [BUILDING.md](BUILDING.md).

## Downloads

Two channels ship from the same byte-reproducible pipeline - both are
on the [GitHub releases page](https://github.com/cubestudio-dev/OpenCubeOS/releases)
and mirrored on the [website downloads page](https://ocos.cubestudio.cn/download.html):

| Channel | Assets |
|---------|--------|
| **development preview** | `opencube-<version>.iso` (boots OCBL → OCOS → ush) |
| **2.2.0** (stable, the 2.x line) | `opencube-2.2.0.iso` · `opencube-2.2.0-src.zip` (deterministic source) · `opencube-2.2.0-update.tar.gz` (OTA) |

Each release note carries the SHA256 of every asset; the website,
the Release and the private mirror hold byte-identical copies.

## Documentation

- [BUILDING.md](BUILDING.md) - build on Linux/Windows, QEMU,
  cross-compilation (ARM64/RISC-V shells), reproducible verification
- [ARCHITECTURE.md](ARCHITECTURE.md) - the L0/L1 architecture, top to bottom
- [INTERFACES.md](INTERFACES.md) - the command surface by home and the L1SVC service tables
  (generated from the tree: `make api-docs`)
- [FAQ.md](FAQ.md) - common questions
- [CONTRIBUTING.md](CONTRIBUTING.md) - ground rules and the PR bar
- The full documentation center (user manual with the mega-app
  guide, developer handbook, driver bring-up guides, internals):
  **https://ocos.cubestudio.cn/docs/**

## Stats

All numbers below are machine-verified at build time
(`make api-docs` writes `docs/data/*.json`; the README verification
command is quoted per item):

- **Source code**: 163,935 lines across 807 files - ocos/ (kernel,
  drivers, services, interface, l1apps) + ocbl/ + ocire/, headers
  included, generated data blobs excluded (the generator caliber:
  `tools/gen_api_docs.py` `STAT_DIRS`/`STAT_EXCLUDE`).  Verify:
  `python3 tools/gen_api_docs.py && python3 -c "import json;d=json.load(open('docs/data/stats.json'));print(sum(v['lines'] for v in d['dirs'].values()))"`
- **Commands**: 311 total - 300 ring-3 registry apps (one command =
  one app = one folder under `ocos/l1apps/`, the 8 merged mega-apps
  included) + 12 ush built-ins + 0 in L0. The registry floor is
  enforced by the regression gate (>= 300).
- **L1SVC service surface**: 20 categories / 208 numbered ops - see
  [INTERFACES.md](INTERFACES.md) for the exact tables.
- **Syscalls**: 54 - verify:
  `grep -c '#define SYS_' ocos/services/sysapi/interface/svc_sysapi_syscall.h`
- **CI**: green on every push - build, ISO, packages, host tests and
  the reproducibility gate
  (`.github/workflows/build.yml`).

## License

Apache 2.0 - see [LICENSE](LICENSE). Every source file carries
`SPDX-License-Identifier: Apache-2.0`.

Copyright 2026 cubestudio-dev &lt;cubestudio@qq.com&gt;
