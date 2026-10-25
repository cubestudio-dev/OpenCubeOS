<!-- GENERATED mirror of the repository-root BUILDING.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Building Open Cube OS

Everything below is executable reality: the file names, targets and
checks come from `Makefile`, `tools/build.sh`, `tools/build.ps1` and
the CI pipeline (`.github/workflows/build.yml`).

## What you get

| Target      | Produces                                        |
|-------------|-------------------------------------------------|
| `make kernel` | `build/opencube.elf` - the kernel ELF (0 warnings, `-Wall -Wextra -Werror`) |
| `make iso`  | `build/opencube.iso` - hybrid BIOS+UEFI bootable ISO |
| `make src-zip` | `dist/OpenCubeOS-<version>-src.zip` - deterministic source zip |
| `make ota`  | `dist/OpenCubeOS-<version>-update.tar.gz` - OTA update package |
| `make all`  | kernel + ISO + SRC zip + OTA                    |
| `make test` | host-side unit tests (`tools/run_host_tests.py`) |
| `make reproducible-check` | two clean builds, byte-identical ELF |
| `make api-docs` | real-tree documentation data (`docs/data/*.json`) |

One-command entries: `tools/build.sh` (Linux; same target names plus
dependency detection and `--install`), `build.ps1` (Windows via WSL2 /
MSYS2).

## Linux

### Dependencies

`gcc, ld (binutils), nasm, grub-mkimage (grub-common), xorriso,
mtools (mformat/mcopy), python3, zip`.

`tools/build.sh` checks all of them and prints the exact Debian package
names for anything missing; `./build.sh --install` apt-installs them
when you are root. Building the ISO additionally needs the GRUB module
trees (`grub-pc-bin` for `i386-pc`, `grub-efi-amd64-bin` for
`x86_64-efi`) and `unicode.pf2` from `grub-common`.

For QEMU testing: `qemu-system-x86`, `seabios`, `ipxe-qemu`, `ovmf`
(UEFI). On a Debian/Ubuntu host:

```
sudo apt-get install gcc nasm xorriso mtools \
     grub-common grub-pc-bin grub-efi-amd64-bin zip python3 \
     qemu-system-x86 ovmf
```

### Build

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
./build.sh            # = make all
# or step by step:
make -j"$(nproc)" kernel
make iso
```

The version banner comes from the `2.0.0` literal in the Makefile
(override with `make OC_RELEASE_VERSION=1.0.1-test1`). The build
number is the commit count plus the +21 alignment offset (see the
Makefile note) so the public sequence stays above the published 274.

### Boot and test

```
make run-bios     # QEMU legacy BIOS boot
make run-uefi     # QEMU UEFI boot (OVMF)
make test         # host unit tests
make shot-bios    # boot screenshot capture
```

Networking on: add `-netdev user,id=n1 -device e1000,netdev=n1` (the
`run-bios` target already does) then `dhcp` inside the guest.

### Reproducible verification

```
make reproducible-check
# reproducible-check: PASS - ELF byte-identical across two clean builds
```

Determinism sources (all verified in-tree): no `__DATE__`/`__TIME__`/
`__TIMESTAMP__` macros, stable `grub-mkimage` output for identical
inputs, fixed link order, `strip --strip-debug` removing path-bearing
sections. The SRC zip is deterministic by construction (`git archive`:
tree order, commit mtimes, index permissions). The OTA package contains
the build day in UTC - it reproduces byte-identically within a day,
which is the documented guarantee.

`SOURCE_DATE_EPOCH` is honoured when exported and forwarded to the
packaging sub-tools.

### Cross-compilation (ARM64 / RISC-V bring-up shells)

```
make ARCH=arm64    # build/arch/arm64/opencube-arm64.elf (PL011 UART shell)
make ARCH=riscv    # build/arch/riscv/opencube-riscv.elf (NS16550A UART shell)
```

These are real bare-metal bring-up shells: PL011 / NS16550A UARTs with
true divisor programming, the ARM generic timer / RISC-V CLINT mtime
counter, and a 1-second heartbeat - banners and incrementing
heartbeats verified live in QEMU (`qemu-system-aarch64 -M virt`,
`qemu-system-riscv64 -M virt`). The full kernel feature set is x86_64
only.

Cross toolchain packages (Debian):
`gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu`,
`gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu`,
`qemu-system-arm`, `qemu-system-misc`.

## Windows

`build.ps1` (PowerShell) forwards the build to the same Makefile from
the best available environment, in this order:

1. **WSL2** (recommended) - a Debian/Ubuntu WSL distribution with the
   Linux dependencies above. The script translates Windows paths with
   `wslpath` and runs `make` inside WSL.
2. **MSYS2** - a locally installed MSYS2 with `mingw-w64` gcc, nasm,
   mtools, xorriso, grub packages on its PATH.
3. Otherwise the script prints the exact packages to install and exits
   non-zero.

```
powershell -ExecutionPolicy Bypass -File build.ps1            # full pipeline
powershell -ExecutionPolicy Bypass -File build.ps1 iso        # kernel + ISO
```

## Multi-configuration builds

```
make defconfig          # write .config (all options on)
make menuconfig         # interactive configurator (tools/oc_menuconfig.sh)
make CONFIG_TUI=n       # one-shot override
```

Options: `CONFIG_TUI`, `CONFIG_CHINESE`, `CONFIG_NVME`, `CONFIG_AHCI`,
`CONFIG_VIRTIO_BLK`. `CONFIG_CHINESE=n` forces `CONFIG_TUI=n`.
A `CONFIG_TUI=n CONFIG_CHINESE=n` build boots with 216 commands
(233 - 17 TUI/Chinese commands); the full configuration boots 233.
The off-path is real code, not deleted code - the configurator's
generated header maps disabled features to no-op equivalents.

## Build cache

The compile cache (`tools/oc_cc.sh`) is content-addressed: the key is
the compiler identity + full flags + the content hash of the source and
of every header dependency. Measured on the release tree: cold 46.6 s
-> warm 14.4 s (186/186 objects hit, 3.2x), warm output byte-identical
to cold. Cache control: `make cache-stats`, `make cache-clear`.

## Scriptable build API

`tools/build.sh` exposes its pipeline as sourceable functions
(`build_all`, `build_iso`, `config_load`, `cache_lookup`,
`cross_compile`, ...) with `tools/build.sh api-selftest` verifying the
8 spec'd interfaces. Release scripts and CI compose these instead of
parsing make output.

## CI

`.github/workflows/build.yml` runs on every push and PR to `main`:
checkout (full history + tags so `git describe` feeds the version
banner), dependency install, kernel (`-Wall -Wextra -Werror`), hybrid
ISO, deterministic SRC zip, OTA package, host test suite, artifact
digests + upload, and the reproducibility gate. QEMU boots and E2E
suites are release-host activities (they need the persisted /etc
volume fixture) - see `docs/release/` for the full release gate.

## Troubleshooting

- **`grub-mkimage: cannot open ... unicode.pf2`** - the font lives in
  `grub-common`; if your distro splits it, point `OC_TOOLS` at a tree
  containing `usr/share/grub/unicode.pf2`.
- **`src-zip: not a git checkout`** - the deterministic source zip
  needs git metadata; build from a clone, or use `make dist` for a
  plain tar-based fallback.
- **QEMU cannot find firmware** - `OC_TOOLS` relocates the whole
  toolchain; QEMU's datadir is probed under `$OC_TOOLS/usr/share/qemu`
  and `$OC_TOOLS/share/qemu`.
- **`setc` / `packed-member` build errors on very new GCC/binutils** -
  fixed in the tree; update past the CI-green commit `fc4e5a5`.
