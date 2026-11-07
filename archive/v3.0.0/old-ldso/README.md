<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Old ld.so (Assembly version)

## Contents
- ld_so.asm (1,715 bytes)

## Why archived
This was the Batch 2 assembly version of ld.so. It was replaced by ld_so.c
(C version, 25,347 bytes) which implements full dynamic linking:
5 relocation types, dlopen/dlsym/dlclose, API table, .dynamic parsing.

## SHA256
Run `sha256sum ld_so.asm` to verify.

## Archived date
2026-09-25
