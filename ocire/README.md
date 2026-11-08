<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocire — Open Cube OS Recovery Kernel

This directory will hold the NAK recovery kernel: the independent,
self-contained kernel used to rescue, diagnose and repair a broken
installation. **It will be implemented in NAK-25.**

NAK-01-tree only establishes the component: `src/placeholder.c` is
compiled through the same zero-warning gate (`-Wall -Wextra -Werror`)
as the kernel to prove the build works; no recovery functionality
exists yet.

- L0 component: provides the recovery service only; no user features.
- Build: `make all` / `make clean` (self-contained Makefile).
