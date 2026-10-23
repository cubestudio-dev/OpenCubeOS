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
- For the E2E suites: Python `pexpect` (regression, watchdog E2E) and
  `paramiko` (SSH E2E).

`tools/build.sh` verifies the build set and prints the exact Debian
package for anything missing; `./build.sh --install` installs them
when you are root. The no-root alternative is
`tools/install-toolchain.sh` (unpacks a relocatable tree and exports
`OC_TOOLS`).

## Getting the tree

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
```

## Sanity build

```
./build.sh iso        # kernel + ISO
make test             # host unit tests
make run-bios         # boot it
```

If all three pass, your environment is good. The full bar for a
change is in [CONTRIBUTING.md](../root/CONTRIBUTING.md).

## Repository layout

```
kernel/    core (sched/syscalls/...), mem (PMM/VMM/heap), lib, crypto, ota
boot/      boot.S - the 32->64-bit entry
arch/      arm64/ riscv/ cross-compilation bring-up shells
drivers/   16 families: block, nic, usb, snd, input, display, pci,
           acpi, i2c, time, wireless, bluetooth, sd, camera, misc, scsi
fs/        VFS + ramfs + fat32 + exfat + ext4
net/       net_core (stack), ssh, sshd, tls, tcp_cc
shell/     the oc> shell + all command modules
l1/        the L1 extension surface headers + solib support
tui/       the terminal UI engine and apps
chinese/   CN console layer (font/encoding/IME)
libs/      ld_so.c - the in-tree shared-object loader
userprogs/ ring-3 test programs (fork/exec/pipe/... )
tools/     build + test + docs generators
tests/     host-side unit tests (make test)
docs/      this documentation tree
```

## Conventions that matter immediately

- `-Wall -Wextra -Werror` - zero warnings, no exceptions.
- Every file carries the SPDX header.
- Kernel-visible strings are ASCII.
- The command surface only grows (see CONTRIBUTING).

## QEMU recipes

Keep these one-liners around:

```
# quick BIOS boot, serial on stdio
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio

# UEFI
qemu-system-x86_64 -m 512 -machine q35 -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/opencube.iso -display none -serial mon:stdio

# with a scratch disk + TUI visible
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \
    -device e1000,netdev=n1

# boot log to a file
... -serial file:boot.log
```

`make run-bios`, `make run-uefi`, `make run-bios-gui`,
`make shot-bios` wrap the common ones (including the /etc volume
setup).
