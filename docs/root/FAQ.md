<!-- GENERATED mirror of the repository-root FAQ.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# FAQ - Open Cube OS

Frequently asked questions, answered from the real code base.
Where a limit is quoted, the header that defines it is named.

## Project

**What is Open Cube OS?**
A from-scratch x86_64 kernel ("L0") that provides services - memory,
scheduling, VFS, network, drivers, and a WinRE-style recovery shell
(the `oc>` prompt) - plus a ring-3 application layer ("L1") whose
default boot entry is the ush user shell. It boots from a hybrid
BIOS+UEFI ISO and gives you **251 command homes** (99 in the L0
recovery shell, 133 ring-3 service commands, 19 ush built-ins) to
inspect and control the machine.

**What is this "L0 / L1" thing?**
Two rings:

- **L0 = provides services.** This repository: the complete kernel,
  the drivers, the service implementations, and the `oc>` recovery
  environment.
- **L1 = uses services.** Ring-3 applications reaching L0 capability
  through the extension surface: the numbered L1SVC op table
  (17 categories / 158 ops, `services/l1/l1svc_defs.h`) plus the
  classic syscalls. `l1apps/` ships the L1 side in-tree: ush (the
  only `OC_L1_APP_DECL` application, the default boot entry) and the
  embedded test programs.

The design principle is "everything is extensible" - the shell, TUI,
drivers and even the console font engine are extension surfaces.

**Is it Linux?**
No. Not a fork, not a distro. Every line of the kernel (130,572 lines
across core/arch/drivers/services/recovery/tui/chinese/boot/l1apps)
is original code under Apache 2.0. It does not run Linux binaries; it
runs its own small ELF user programs (see `l1apps/` and the `run`
command).

**What license?**
Apache 2.0 (see `LICENSE`). Every source file carries an
`SPDX-License-Identifier: Apache-2.0` header.

## Booting

**How do I boot it?**
```
qemu-system-x86_64 -m 512 -cdrom opencube-<version>.iso
```
or write the ISO to a USB stick and boot real hardware. The ISO is
hybrid: it boots through legacy BIOS (El Torito) and UEFI (an EFI boot
image) alike - `make run-bios` / `make run-uefi` do the QEMU variants.
The default boot lands in the ring-3 `ush>` prompt; `exit` drops into
the `oc>` recovery shell.

**Does it need a hard disk?**
No. The ISO boots self-contained (the kernel is attached as a multiboot2
module). A disk only matters if you want the A/B update slots, /etc
persistence volume, or the `install` command's target disk.

**Why does the boot menu show slot A / slot B / slot ISO?**
That is the A/B update scheme (`services/ota/ota_ab.{c,h}`). GRUB reads a
boot-slot flag and boots the active slot; `abcfg`/`ab_partition_test`
and the OTA commands control it from inside the system.

## The shell

**How many commands?**
251 command homes: 99 L0 recovery-shell commands (`help -a` at the
`oc>` prompt), 133 ring-3 SVC commands routed through SYS_L1SVC, and
19 ush built-ins (`help` inside ush). Verify live in the guest, or
`make api-docs` and read `docs/data/commands.json`. The v1.0.0
baseline of 233 commands is mapped whole - see the compatibility
table in `INTERFACES.md` Part 1c.

**Where is the full command list?**
At `oc>`: `help -a` (alphabetical), `help -w` (by work-package),
`help` (short list). Inside ush: `help`. The reference documentation
under `docs/manual/commands/` covers all 251.

**Why are all system strings English?**
The console and kernel paths are ASCII by design (see
`CONTRIBUTING.md` ground rule 1); a separate Chinese console/TUI layer
exists (`chinese/` tree, `chinese` command), but the system's own
strings stay ASCII for hardware compatibility.

## Limits and design choices

**How much RAM does it need?**
It runs in 512 MiB QEMU guests comfortably. The physical memory manager
parses the multiboot2 memory map and manages whatever the bootloader
reports; there is no fixed ceiling in the PMM.

**Why 100 Hz timer?**
The PIT is programmed at 100 Hz (`core/core_timer.h`) - 10 ms
ticks drive the scheduler's round-robin slices and the soft-timer
wheel. HPET and the TSC are additionally supported as time sources
(`hpet`, `pmtimer`, `tsc` commands) but the PIT remains the tick
source.

**Can commands be removed?**
Not silently - the interface contract says the command surface only
grows; any real deletion must be declared in the deletion register
(`INTERFACES.md` Part 2d; the v2.0.0 tree removed only five init/probe
plumbing entries, each declared with its replacement). The L0
registry cap is `SHELL_MAX_COMMANDS` (256, with 99 registered at
2.2.0 - headroom for growth; the limit lives in
`recovery/shell.h`).

**Where do my files go?**
`ramfs` (in-memory, volatile) by default; FAT32 / exFAT / ext4 volumes
you mount or format (`mkfs.fat32` etc. from ush) are persistent; the
32 MiB `/etc` volume holds config across reboots (`etc.img`,
`opencube.conf`).

## Networking

**What does the network stack do?**
Ethernet + ARP + IPv4 + ICMP + UDP + DNS + TCP (with congestion
control), sockets for user programs, an HTTPS client (TLS 1.2/1.3 with
RSA/EC certificate verification), an SSH client, and an SSH *server*
(`sshd` on the guest, keys on the /etc volume). The NIC layer supports
e1000/e1000e/igb/ixgbe/rtl8139/rtl8169(rtl8168/8125/810x families) and
BCM57xx cards; QEMU's default `-device e1000` works out of the box.

**How do I get network in QEMU?**
```
qemu-system-x86_64 -m 512 -cdrom opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1
```
then inside the guest: `dhcp`, `ping 10.0.2.2`, `wget 10.0.2.2`,
`dns example.com`.

## Debugging

**How do I see the kernel log?**
`dmesg` (ring buffer), `logcat` (live follow), `logctl` (level control
and filters) - the logging subsystem is `services/klog/lib_klog.{c,h}`
with rotation (`kernel.log`, `kernel.log.1`, ... on the /etc volume).

**It triple-faults / reboots on my machine - what now?**
Boot with `-serial file:log.txt` and read the last self-test line that
did NOT print `OK`; that is the failing subsystem. The crash recovery
path (`crashtest`, `dumpinfo`, crashlog on the /etc volume) captures
panics with register dumps.

**How do I build with debug symbols?**
The default build already carries `-g` and links, then strips debug
symbols from the final ELF; build artifacts before `strip` remain in
`build/` during the pipeline (`opencube_base.elf`).

## Build & reproducibility

**Are builds reproducible?**
Yes - `make reproducible-check` builds the kernel twice from clean and
byte-compares the ELF; it must print `PASS`. There are no
`__DATE__`/`__TIME__`/`__TIMESTAMP__` macros in the tree, link order is
fixed, and `strip` removes path-bearing sections. The source zip is a
deterministic `git archive` (tree order, commit mtimes, index
permissions). The OTA package embeds the build day (UTC) - it is
reproducible within a day, by design.

**Why does CI matter for a hobby kernel?**
The CI gate (`.github/workflows/build.yml`) is the portable truth: a
stricter GCC, a newer binutils or a newer GRUB on the runner can reject
what a local toolchain silently accepts - that is exactly how the
2.0.0 release shipped source that compiled locally but not on CI
(port-constraint asm, a retired GRUB module, the C23 `bool` keyword;
all fixed in 2.1.0). Since 2.1.0 the gate is hard: the release tag
must point at a CI-green commit.

**Windows?**
`build.ps1` drives the same pipeline through WSL2 (recommended) or an
MSYS2 toolchain - see `BUILDING.md`.

## Extending

**How do I write an L1 extension?**
Read `docs/l1/` (the in-tree guide set: overview, surface map,
writing-modules, the driver/block/nic/snd/ota/watchdog guides). The
numbered L1SVC surface (17 categories / 158 ops) is defined in
`services/l1/l1svc_defs.h`; the still-public register-class surfaces
(`tui/tui_ext.h`, `services/watchdog/core_watchdog.h`,
`services/l1/l1_ext.h` and friends) and the per-service headers are
documented in `INTERFACES.md`.

**Can I add my own shell command from a user program?**
Yes - `l1_ext_shell_register_command_ex` is exposed through the L1
surface (`services/l1/l1_ext.h`), and the dynamic loader supports
L1 shared objects (`.so` via `l1apps/ld_so.c`, try `so_test` and
`dlsym_test` in the guest).
