<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Archive

This directory contains archived files from the Open Cube OS project — old
source trees, superseded versions, and historical artifacts that are no longer
part of the active build but are kept for reference.

## Contents

| Directory | What | Why archived | Date |
|-----------|------|-------------|------|
| old-src-tree/ | Root-level source tree from handoff zip extraction (WP-08a era) | Superseded by src-WP08a/ content (now at repo root). Contains dynlink.c/h (WP-08b WIP in-kernel linker, replaced by user-space ld.so). | 2026-09-25 |
| WP06/ | OpenCubeOS-src-WP06-20260921-101230.zip | Stale WP-06 source package. Deduplicated (was in both root and src-WP08a/). | 2026-09-25 |
| old-ldso/ | ld_so.asm | Batch 2 assembly version of ld.so. Replaced by ld_so.c (C version with full dynamic linking). | 2026-09-25 |
| old-tests/ | dyn_test.elf | Old copy of so_test.elf (identical bytes). dyn_test is now an alias for so_test in cmd_run. | 2026-09-25 |
| v3.0.0/ | The full pre-NAK tree mirror (src/L0 + src/L1 + tools + docs + website snapshot, maintained as a dual-write mirror through the 3.0.0 work packages) | REMOVED in WP-refactor-to-nak-architecture: the NAK restructure moved the active tree to ocos/ and the mirror became a stale duplicate of a dead layout. The complete v3.0.0-era snapshot stays recoverable from git history and the release tags. | 2026-11-14 |

## SHA256 Checksums

See each subdirectory's README.md for per-file SHA256 checksums.
