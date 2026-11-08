/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL BIOS disk/memory layout (WP-02-ocbl).
 *
 * THE single source of truth for the addresses the BIOS path
 * agrees on across stage 1, stage 2a, stage 2b and the 64-bit C
 * payload. Nothing in the BIOS path may hardcode these values
 * directly - include this header instead.
 *
 * It is included by assembly (via the C preprocessor) and by C code
 * alike.
 *
 * Naming: [class]_[category]_[role] - CORE_BOOT_BIOS_* macros.
 */

#ifndef CORE_BOOT_BIOS_LAYOUT_H
#define CORE_BOOT_BIOS_LAYOUT_H

/* first LBA of the kernel.elf area inside ocbl-bios.bin */
#define CORE_BOOT_BIOS_KERNEL_LBA   64

/* physical address the kernel ELF is streamed to in real mode */
#define CORE_BOOT_BIOS_KERNEL_BASE  0x00020000

/* link/load address of the 64-bit payload (stage 2b + C code):
 * inside the first 64 KiB segment (stage 1 loads stages 2a+2b with
 * ONE extended read of 64 sectors into 0x7E00..0xFE00) */
#define CORE_BOOT_BIOS_64_ENTRY     0x0000BC00

/* 64-bit stack top for the C payload (kernel area stops at 256 KiB) */
#define CORE_BOOT_BIOS_STACK_TOP    0x00080000

/* maximum kernel.elf size the disk image reserves: 512 sectors */
#define CORE_BOOT_BIOS_KERNEL_MAX_SECTORS 512

#endif /* CORE_BOOT_BIOS_LAYOUT_H */
