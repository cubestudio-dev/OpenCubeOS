<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-03 Build Guide

## Prerequisites

WP-03 needs a freestanding x86_64 toolchain: a C compiler, an assembler, a linker, an ISO builder, QEMU, and OVMF (UEFI firmware). On a stock Debian/Ubuntu system these come from `gcc`, `nasm`, `xorriso`, `grub-pc-bin`, `grub-efi-amd64-bin`, `grub-common`, `qemu-system-x86`, `ovmf`, `mtools`, and `python3-pil`.

After installing, the following must all report versions:

```sh
gcc --version          # any recent gcc
nasm --version         # 2.16+
xorriso --version      # 1.5+
grub-mkrescue --version # 2.12+
grub-mkimage --version  # 2.12+
qemu-system-x86_64 --version # 10.0+
# OVMF firmware files (location varies by distro):
#   Debian/Ubuntu: /usr/share/OVMF/OVMF_CODE_4M.fd and OVMF_VARS_4M.fd
#                  or /usr/share/ovmf/OVMF.fd
```

Set the following environment variables if your tools are not in the default
system paths (e.g. you extracted them to a custom location):

```sh
export OC_TOOLS=/path/to/your/toolchain/extract
export OVMF_CODE=/path/to/OVMF_CODE.fd
export OVMF_VARS=/path/to/OVMF_VARS.fd
```

The build scripts (`tools/build_iso.sh`, `tools/qemu_shot_vnc.py`) read
`OC_TOOLS` to locate GRUB modules, SeaBIOS, and QEMU data files.

## Build

```sh
cd /path/to/oc-os
make            # builds build/opencube.elf
make iso        # builds build/opencube.iso (hybrid BIOS+UEFI)
```

The `iso` target calls `tools/build_iso.sh`, which:

1. Stages the kernel ELF, `grub.cfg`, the unicode font, and GRUB's `i386-pc` and `x86_64-efi` modules into `build/iso-stage/`.
2. Calls `grub-mkimage -O i386-pc-eltorito` to build the BIOS El Torito boot image.
3. Calls `grub-mkimage -O x86_64-efi` to build `EFI/BOOT/BOOTX64.EFI`.
4. Builds a 1 MiB FAT image containing `EFI/BOOT/BOOTX64.EFI` - the EFI System Partition (ESP).
5. Calls `xorriso -as mkisofs` with both `-b` (BIOS El Torito) and `-e` (UEFI ESP) options to produce the final hybrid ISO.

The resulting ISO has **two** El Torito boot entries:

```
El Torito boot img :   1  BIOS  y   none  0x0000  0x00      4         579
El Torito boot img :   2  UEFI  y   none  0x0000  0x00   2048          67
```

BIOS firmware picks entry 1, UEFI firmware picks entry 2.

## The persistent /etc volume (build/etc.img) - rebuilt per test run, never committed

The SSH and HTTPS end-to-end suites (`tools/ssh_mainstream_test.py`,
`tools/https_test_server.py` runs, `tools/test_bug0132_ctrlc.py`) attach a
persistent `/etc` volume to the QEMU guest as a second IDE drive:

```sh
-drive if=ide,format=raw,file=build/etc.img
```

Specification and purpose:

- **32 MiB FAT32 image, volume label `OCS_ETC`.** The kernel mounts it as
  `/etc` at boot (WP-09-fix5 `lib_config_init()`); without it the kernel
  falls back to a ramfs `/etc`.
- Holds `/etc/opencube.conf` plus the per-installation SSH material
  (BUG-0075 trust model): `ssh_host_key` (sshd host key),
  `ssh_client_key` (client identity for publickey auth) and
  `ssh_authorized_keys` (the moduli sshd trusts).

Why it is NOT in git: `.gitignore` excludes the whole `build/` tree
(build/runtime products), and `etc.img` carries private key material that
must be **per-installation**. Committing it would ship one shared host key
to every checkout - exactly what the p1fix2 BUG-0075 fix removed from the
kernel. Treat it like a generated machine identity: recreate it locally.

Recreate it after a fresh clone / sandbox reset (run from the repo root,
`mtools` on `$PATH` or under `$OC_TOOLS/usr/bin`):

```sh
mformat -i build/etc.img -C -F -T 65536 -v OCS_ETC   # 65536 x 512 B = 32 MiB, FAT32 (-F)
python3 tools/host_keygen_etc.py                     # host RSA-2048 keygen + inject
                                                     # ssh_host_key / ssh_client_key /
                                                     # ssh_authorized_keys
```

Without `etc.img` the first sshd start falls into the guest's one-time
Miller-Rabin keygen (minutes under TCG), which usually times out the test
harness' banner wait - a 0/5 SSH matrix result that is an environment
problem, not a kernel regression. With it, sshd starts instantly from the
pre-injected key.

## Run

```sh
make run-bios    # SeaBIOS via -cdrom
make run-uefi    # OVMF via -cdrom (with -boot d)
```

Both targets dump the kernel's serial output to stdout. The kernel's COM1 (port 0x3F8) is wired to the console output hook, so every line that's drawn on the framebuffer is also tee'd to serial.

## Screenshots

```sh
make shot-bios   # -> build/shot-bios.png  (800x600)
make shot-uefi   # -> build/shot-uefi.png  (800x600)
```

These use `tools/qemu_shot_vnc.py`, which boots QEMU with a VNC display, waits for the kernel to finish booting, then sends `screendump` to QEMU's monitor socket. The PPM is converted to PNG via PIL.

## Distribution

```sh
make dist
```

Produces:

- `dist/OpenCubeOS-src-WP<NN>-<timestamp>.zip` - source tree (excludes `build/`, `dist/`, `.git/`)
- `dist/OpenCubeOS-WP<NN>-<timestamp>.iso` - copy of `build/opencube.iso`
- `dist/OpenCubeOS-WP<NN>-<timestamp>.iso.sha256` - SHA256 checksum

## Clean

```sh
make clean
```

Removes `build/`, `dist/`, and the staged kernel ELF under `iso/`.
