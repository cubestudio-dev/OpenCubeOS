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

## Repository layout (NAK)

```
OpenCubeOS/
├── ocos/       main system (kernel / drivers / services / l1apps / apps)
├── ocbl/       bootloader
├── ocse/       install environment
├── ocire/      recovery kernel
├── archive/    frozen legacy trees (NOT built by NAK)
├── docs/       documentation center (user / developer / system)
├── wiki/       wiki sources
├── website/    website sources
└── tools/      shared build tools
```

## Status

NAK work package `NAK-01-tree` is done: the directory skeleton, a
clean Make build system and a minimal `ocos.elf` kernel stub. The
legacy 3.0.0 tree is fully preserved under `archive/v3.0.0/` and is
not part of any NAK build.

## Build

```
make all      # build every component (today: ocos.elf stub)
make clean    # remove all build products
```

Requirements: `gcc`, `binutils`, GNU `make`.

## License

Apache 2.0 — Copyright `cubestudio-dev <cubestudio@qq.com>`.
