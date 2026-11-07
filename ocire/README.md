<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocire — Open Cube OS Recovery Kernel

This directory will hold the NAK recovery kernel: the independent,
self-contained kernel used to rescue, diagnose and repair a broken
installation. NAK-01-tree only establishes the directory; no recovery
code exists yet.

- L0 component: provides the recovery service only; no user features.
- Built by its own Makefile once the recovery kernel lands in a later
  work package. Today `make` recurses into it as a no-op placeholder.
