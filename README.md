<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS

Open Cube OS is a service-oriented operating system. The project is
being rewritten from the ground up; the new kernel's internal codename
is **NAK** (New Architecture Kernel). The public name stays
**Open Cube OS**.

## Architecture baseline

- **L0 — provides services**: kernel, drivers, root services and the
  extension interfaces. L0 has no user-facing features at all.
- **L1 — consumes services**: every Ring 3 user application, using L0
  exclusively through its extension interfaces.
- **Core idea**: everything is extensible.

## Source naming convention

Every source file and every external symbol follows one scheme,
`[class]_[category]_[role]`, all lowercase for files, all uppercase
for macros:

- `core_*` — kernel core (`core_main_entry.c`, `core_io_port.h`,
  `core_boot_multiboot2.S`, symbols like `core_main_entry()`,
  macros like `CORE_MAIN_VERSION`)
- `drv_*` — drivers (`drv_video_vga.c`, `drv_serial_16550.c`,
  symbols like `drv_serial_puts()`)
- `svc_*` — services (`svc_fs_vfs.c`, API headers under
  `ocos/interface/` named `svc_<name>_api.h`, symbols like
  `svc_fs_mount()`)
- Structs end in `_t` (`core_mm_pmm_t`); macros are uppercase.

`_start`/`_start64` (ELF entry points), `linker.ld` (link script)
and `grub.cfg` (GRUB configuration) keep their ecosystem-standard
names.

## Repository layout (NAK)

```
OpenCubeOS/
├── ocos/       main system (kernel / drivers / services / interface / l1apps / apps)
├── ocbl/       bootloader
├── ocse/       install environment
├── ocire/      recovery kernel
├── archive/    frozen legacy trees (NOT built by NAK, not in src zips)
├── docs/       documentation center (user / developer / system)
├── wiki/       wiki sources
├── website/    website sources
└── tools/      shared build tools
```

## Status

NAK work package `WP-01-tree`: the directory skeleton, the Make build
system and the bootable skeleton kernel are in place. `make -C ocos
all` produces `build/kernel.elf` and
`build/opencubenak-WP-01-tree.iso` (GRUB + Multiboot2); booting the
ISO in QEMU prints the four-line boot banner on the VGA screen and on
COM1. The legacy 3.0.0 tree is fully preserved under `archive/v3.0.0/`
and is not part of any NAK build.

## Build

```
make all      # build every component (kernel.elf + opencubenak-WP-01-tree.iso + placeholders)
make srczip   # build/opencubenak-WP-01-tree-src.zip from the committed tree
make clean    # remove all build products
```

Requirements: `gcc` (14.x), `binutils`, GNU `make`, `nasm`, `xorriso`,
`mtools`, `grub-pc-bin` + `grub-common`, and `qemu-system-x86` for
`make -C ocos run`. A no-root installer for the extra tools is
`tools/install-toolchain.sh` (uses apt download + dpkg extraction into
a user prefix; no root needed).

## License

Apache 2.0 — Copyright `cubestudio-dev <cubestudio@qq.com>`.
