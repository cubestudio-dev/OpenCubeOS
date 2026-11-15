<!-- SPDX-License-Identifier: Apache-2.0 -->
# Development environment

How to go from a fresh machine to hacking on the kernel - the
toolchain contract, the two dependency gates, the no-root sandbox
install, and the layout you will be navigating.

## 1. Requirements

- A Linux host (Debian/Ubuntu tested; anything with the packages
  below works). Windows: WSL2 gives you exactly this environment
  (`build.ps1` dispatches to it; MSYS2 lacks xorriso/grub).
- **The release toolchain**: gcc-14 (Debian gcc 14.2.0 is the
  compiler the releases are built with; CI prefers `gcc-14` with a
  `gcc` fallback), binutils `ld`, **nasm 2.16.03**, xorriso
  **1.5.6**, mtools, grub-common + grub-pc-bin +
  grub-efi-amd64-bin, python3 (3.9+), zip.
- For testing: **qemu-system-x86 10.0.13**, **OVMF**, seabios,
  ipxe-qemu.
- For the E2E suites: Python `pexpect` (regression, mega-app
  smoke) and `paramiko` (SSH E2E).
- Cross bring-up shells only: `gcc-aarch64-linux-gnu`,
  `gcc-riscv64-linux-gnu` (GCC >= 12).

Two different "check_deps" exist - do not confuse them:

1. **Host-package check** - `tools/build.sh` verifies the build
   set (gcc, ld/binutils, nasm, grub-mkimage/grub-common, xorriso,
   mformat+mcopy/mtools, python3, zip), printing the exact Debian
   package per command; `--install` apt-installs them as root,
   otherwise it points at the no-root alternative below.
2. **Architecture gate** - `tools/check_deps.py` enforces the
   *dependency direction* over
   `ocos/{kernel,drivers,services,l1apps}`:
   - kernel must not include anything resolving under `drivers/`
     or `services/` (exception: the `core_boot_*.c` composition
     root),
   - drivers must not include services,
   - l1apps may only include `ocos/interface/`-owned headers,
   - plain-basename `#include "x.h"` resolution against a real
     file inventory; any violation exits 1 with file:line, the
     include, and the resolved owner.
   Run it before release hygiene (it is not wired into CI).

## 2. The no-root sandbox install

`tools/install-toolchain.sh` downloads Debian `.deb`s and extracts
them with `dpkg -x` into `$OC_OPT` (default
`/home/z/opt/extract`) - ~117 packages: nasm 2.16.03,
qemu-system-x86 10.0.13 (+ data + libs), xorriso 1.5.6,
grub-pc-bin + grub-efi-amd64-bin + grub-common, mtools, ovmf,
seabios. gcc itself comes from the system. Then:

```
source tools/env-sandbox.sh   # exports OC_TOOLS, PATH, LD_LIBRARY_PATH
```

or point the Makefile at the tree directly:
`make OC_TOOLS=<tree> ...` - it probes `<tree>/usr/...` and
`<tree>/...` layouts; `OVMF_FD`/`QEMU_DATADIR`/`SEABIOS_DIR`
derive from `OC_TOOLS` and can be overridden individually.

## 3. Getting the tree and the sanity build

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
make all             # userprogs + kernel + ISO + src zip + OTA pkg
make test            # host unit tests
make run-bios        # boot it
```

If all three pass, your environment is good. Note CI clones with
`fetch-depth: 0` - the build id is `git rev-list --count HEAD + 21`, and a shallow clone would bake a build id below the published
ones (every installed machine would ignore the release as a
"downgrade"). The full bar for a change is in
[CONTRIBUTING.md](../root/CONTRIBUTING.md).

## 4. Repository layout

```
ocos/kernel/      L0 core: boot chain, sched, PMM/VMM/heap, usermode
ocos/kernel/arch/x86_64/  the 64-bit arch layer (cross archs in build.mk)
ocos/services/    one folder per service facet: block, net, audio,
                  fs, selfhost (OTA), power, diag, health, klog,
                  watchdog, display, tui, crypto, camera, usb, ...
ocos/drivers/     one folder per class: storage, net, audio, bus
                  (pci/acpi/i2c), usb, input, display, time, misc
ocos/interface/   the ring-3 contract headers: l1_lib.h,
                  l1svc_ops.h, i18n.h, wizard_lib.h, fs_lib.h,
                  editor_core.h, oc_l1_api.h, crt0.asm
ocos/l1apps/      one command = one app = one folder (284 programs)
                  + _i18n/ for the flat legacy test programs
ocire/            the independent recovery kernel (second L0)
ocbl/             the in-tree bootloader (BIOS stage1 + EFI entry)
ocse/             the vendored install-environment kernel
tools/            build + test + docs generators (117 files)
tests/            host-side unit tests (make test)
docs/             this documentation tree (en + zh mirror)
etc/              the default /etc volume content + l0 i18n
```

## 5. Conventions that matter immediately

- `-Wall -Wextra -Werror` - zero warnings, no exceptions, on every
  component.
- Every file carries the SPDX header.
- Kernel/service-visible strings are ASCII English (they are the
  script + wizard-anchor contract); human surfaces render through
  `tr_()` with en+zh resources.
- The command surface only grows (interface freeze; the numbering
  is append-only - see
 [interface-dictionary.md](interface-dictionary.md)).
- Big buffers live on the heap (kernel) or static BSS (ring 3) -
  the kernel image is pinned below 0x400000 (see
  [build.md](build.md) section 4).

## 6. QEMU recipes

```
# quick BIOS boot, serial on stdio
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio -display none

# UEFI (OVMF; the make target copies OVMF.fd to a pflash work copy)
qemu-system-x86_64 -m 512M -machine q35 \
    -drive if=pflash,format=raw,file=OVMF_WORK.fd \
    -cdrom build/opencube.iso -display none -serial mon:stdio

# with a scratch disk + display for the TUI
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \
    -device e1000,netdev=n1

# boot log to a file
... -serial file:boot.log
```

`make run-bios`, `make run-uefi`, `make run-bios-gui`,
`make run-uefi-gui`, `make run-bios-persist`, `make shot-*` wrap
the common ones (including the fresh 32 MiB `/etc` volume setup so
persistence tests start clean). A relocated toolchain:
`make OC_TOOLS=<tree> run-bios`. Deeper QEMU guidance - errata,
sendkey, firmware paths - lives in
[qemu-testing.md](qemu-testing.md).

## 7. Host unit tests

`make test` (`tools/run_host_tests.py`) extracts the gcc line from
each `tests/host_*.c` header, remaps pre-restructure paths,
compiles and runs on the host compiler: crypto vectors (NIST
AES/SHA/HMAC, RSA, X.509, AEAD), TLS key schedule, TCP congestion
control, block-512 gate, multiboot2 parsing, BT/WiFi/camera/SCSI/
IR/touchpad subsystems. Several harnesses use ASAN locally;
fixture-dependent ones (openssl RSA/chain fixtures) declare
SKIP-FIXTURE when the fixtures are absent. A regression here is
much faster to iterate on than a boot loop. Full list:
[host-tests.md](host-tests.md).
