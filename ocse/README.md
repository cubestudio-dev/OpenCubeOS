<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocse — Open Cube OS Install Environment

This directory will hold the NAK install-environment kernel: the
standalone environment that prepares a target machine for Open Cube OS
(disk setup, image staging, first-boot configuration). **It will be
implemented in NAK-24.**

NAK-01-tree only establishes the component: `src/placeholder.c` is
compiled through the same zero-warning gate (`-Wall -Wextra -Werror`)
as the kernel to prove the build works; no installer functionality
exists yet.

- L0 component: provides the install service only; no user features.
- Build: `make all` / `make clean` (self-contained Makefile).
