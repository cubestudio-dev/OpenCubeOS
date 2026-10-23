<!-- SPDX-License-Identifier: Apache-2.0 -->
# FAQ - Open Cube OS

Frequently asked questions, answered from the real code base.
Where a limit is quoted, the header that defines it is named.

## Project

**What is Open Cube OS?**
A from-scratch x86_64 kernel ("L0") plus a WinRE-style recovery shell
(the `oc>` prompt). It boots from a hybrid BIOS+UEFI ISO, brings up its
own memory management, scheduler, VFS, network stack and drivers, and
gives you 233 shell commands to inspect and control the machine.

**What is this "L0 / L1" thing?**
Two-layer architecture:

- **L0** = this repository: the complete kernel plus the recovery shell.
- **L1** = upper-layer extensions. L0 exposes 162 numbered extension
  interfaces (plus a documented set of driver/OTA/watchdog/health
  headers); L1 modules are *not* shipped with L0.

The design principle is "everything is extensible" - the shell, TUI,
drivers and even the console font engine are extension surfaces.

**Is it Linux?**
No. Not a fork, not a distro. Every line of the kernel (140,672 lines
across kernel/boot/drivers/fs/net/shell/l1/tui/chinese/arch/userprogs/libs)
is original code under Apache 2.0. It does not run Linux binaries; it
runs its own small ELF user programs (see `userprogs/` and the
`run` command).

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

**Does it need a hard disk?**
No. The ISO boots self-contained (the kernel is attached as a multiboot2
module). A disk only matters if you want the A/B update slots, /etc
persistence volume, or the `install` command's target disk.

**Why does the boot menu show slot A / slot B / slot ISO?**
That is the A/B update scheme (`kernel/ota/ota_ab.{c,h}`). GRUB reads a
boot-slot flag and boots the active slot; `abcfg`/`ab_partition_test`
and the OTA commands control it from inside the system.

## The shell

**How many commands?**
233 (verify live: `help -a` inside the guest, or
`make api-docs` and read `docs/data/commands.json`).

**Where is the full command list?**
`help -a` (alphabetical), `help -w` (by work-package),
`help` (short list). The reference documentation under `docs/manual/`
covers every domain.

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
The PIT is programmed at 100 Hz (`kernel/core/core_timer.h`) - 10 ms
ticks drive the scheduler's round-robin slices and the soft-timer
wheel. HPET and the TSC are additionally supported as time sources
(`hpet`, `pmtimer`, `tsc` commands) but the PIT remains the tick
source.

**Can commands be removed?**
No - the interface contract says the command surface only grows
(`SHELL_MAX_COMMANDS` is 256 with 233 registered, headroom for 23 more;
the limit lives in `shell/shell.h`).

**Where do my files go?**
`ramfs` (in-memory, volatile) by default; FAT32 / exFAT / ext4 volumes
you mount or format (`mkfs.fat32` etc.) are persistent; the 32 MiB
`/etc` volume holds config across reboots (`etc.img`, `opencube.conf`).

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
and filters) - the logging subsystem is `kernel/lib/lib_klog.{c,h}`
with rotation (`kernel.log`, `kernel.log.1`, ... on the /etc volume).

**It triple-faults / reboots on my machine - what now?**
Boot with `-serial file:log.txt` and read the last self-test line that
did NOT print `OK`; that is the failing subsystem. The crash recovery
path (`crashtest`, `dumpinfo`, crashlog on the /etc volume) captures
panics with register dumps. `tools/run_fix4_regression.py` shows the
expected boot sequence in full.

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
stricter GCC or a newer binutils on the runner once caught three real
portability defects that local toolchains silently accepted. The gate
runs the exact targets you run locally.

**Windows?**
`build.ps1` drives the same pipeline through WSL2 (recommended) or an
MSYS2 toolchain - see `BUILDING.md`.

## Extending

**How do I write an L1 extension?**
Include the surface headers (`l1/l1_ext.h`, `l1/l1_wp8a.h`, ...,
`tui/tui_ext.h`, the driver/OTA/watchdog/health headers), implement
your module, and register it. The 162 numbered interfaces and their
groupings are in `INTERFACES.md`; the extension guide lives under
`docs/l1/`.

**Can I add my own shell command from a user program?**
Yes - `shell_register_command_ex` is exposed through the L1 surface
(`l1_ext_shell_register_command_ex`), and the dynamic loader supports
L1 shared objects (`.so` via `libs/ld_so.c`, try `so_test` and
`dlsym_test` in the guest).
