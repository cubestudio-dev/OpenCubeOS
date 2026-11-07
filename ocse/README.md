<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# ocse — Open Cube OS Install Environment

This directory will hold the NAK install-environment kernel: the
standalone environment that prepares a target machine for Open Cube OS
(disk setup, image staging, first-boot configuration). NAK-01-tree only
establishes the directory; no installer code exists yet.

- L0 component: provides the install service only; no user features.
- Built by its own Makefile once the installer lands in a later work
  package. Today `make` recurses into it as a no-op placeholder.
