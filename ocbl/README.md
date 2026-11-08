<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocbl — Open Cube OS Bootloader

This directory will hold the NAK bootloader (second-stage loader, disk
I/O, kernel hand-off). **It will be implemented in NAK-02.**

NAK-01-tree only establishes the component: `src/placeholder.c` is
compiled through the same zero-warning gate (`-Wall -Wextra -Werror`)
as the kernel to prove the build works; no bootloader functionality
exists yet.

- L0 component: provides the boot service only; no user features.
- Build: `make all` / `make clean` (self-contained Makefile).
