<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocbl — Open Cube OS Bootloader

This directory will hold the NAK bootloader (second-stage loader, disk
I/O, kernel hand-off). NAK-01-tree only establishes the directory; no
bootloader code exists yet.

- L0 component: provides the boot service only; no user features.
- Built by its own Makefile once the bootloader lands in a later
  work package. Today `make` recurses into it as a no-op placeholder.
