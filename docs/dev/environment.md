<!-- SPDX-License-Identifier: Apache-2.0 -->
# Development environment

How to go from a fresh machine to hacking on the kernel.

## Requirements

- A Linux host (Debian/Ubuntu tested; anything with the packages
  below works). Windows: WSL2 gives you exactly this environment.
- `gcc` (14.x tested), `binutils` (ld), `nasm` 2.16+, `xorriso`,
  `mtools`, `grub-common` + `grub-pc-bin` + `grub-efi-amd64-bin`,
  `python3` (3.9+), `zip`.
- For testing: `qemu-system-x86`, `ovmf`, `seabios`, `ipxe-qemu`.
- For the E2E suites: Python `pexpect` (regression, mega-app smoke)
  and `paramiko` (SSH E2E).

`tools/build.sh` verifies the build set and prints the exact Debian
package for anything missing; `./build.sh --install` installs them
when you are root. The no-root alternative is
`tools/install-toolchain.sh` (unpacks a relocatable tree and exports
`OC_TOOLS` - point the Makefile at it with `make OC_TOOLS=<tree>`;
it probes `<tree>/usr/...` and `<tree>/...` layouts).

## Getting the tree

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
```

## Sanity build

```
make all             # userprogs + kernel + ISO + src zip + OTA pkg
make test            # host unit tests
make run-bios        # boot it
```

If all three pass, your environment is good. The full bar for a
change is in [CONTRIBUTING.md](../root/CONTRIBUTING.md).

## Repository layout

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
                  editor_core.h
ocos/l1apps/      one command = one app = one folder (284 programs)
                  + _i18n/ for the flat legacy test programs
ocire/            the independent recovery kernel (second L0)
tools/            build + test + docs generators
tests/            host-side unit tests (make test)
docs/             this documentation tree (en + zh mirror)
etc/              the default /etc volume content + l0 i18n
```

## Conventions that matter immediately

- `-Wall -Wextra -Werror` - zero warnings, no exceptions.
- Every file carries the SPDX header.
- Kernel/service-visible strings are ASCII English (they are the
  script + wizard-anchor contract); human surfaces render through
  `tr_()` with en+zh resources.
- The command surface only grows (interface freeze, see
  CONTRIBUTING).

## QEMU recipes

Keep these one-liners around:

```
# quick BIOS boot, serial on stdio
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio

# UEFI
qemu-system-x86_64 -m 512M -machine q35 -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/opencube.iso -display none -serial mon:stdio

# with a scratch disk + TUI visible
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \
    -device e1000,netdev=n1

# boot log to a file
... -serial file:boot.log
```

`make run-bios`, `make run-uefi`, `make run-bios-gui`,
`make shot-bios` wrap the common ones (including the /etc volume
setup). A relocated toolchain: `make OC_TOOLS=<tree> run-bios`
(and `OVMF_FD=...` when the tree layout differs).
