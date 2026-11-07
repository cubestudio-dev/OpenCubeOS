/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */

/* libfoo.c - shared library for WP-08b dynamic linking tests.
 * Exports:
 *   foo_add(int, int) → int   (function, for JUMP_SLOT/GLOB_DAT relocs)
 *   foo_global = 42           (global var, for COPY reloc testing)
 */
int foo_add(int a, int b) { return a + b; }
int foo_global = 42;
