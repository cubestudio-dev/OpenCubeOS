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

/* physical address the kernel ELF lives at when the 64-bit payload
 * starts. Two flavors share this layout:
 *
 *  - disk flavor (default): stage 2a streams the kernel from disk to
 *    0x20000 in real mode (INT 13h AH=42h, 64-sector batches, count
 *    patched into ocbl_kernel_sectors by the Makefile);
 *
 *  - El Torito no-emulation flavor (OCBL_BOOT_CD, the BIOS boot
 *    image inside opencubenak-WP-02-ocbl.iso): the firmware loads
 *    the WHOLE boot image - stage 1, stage 2a, stage 2b and the
 *    kernel at image LBA 64 (file offset 32 KiB) - contiguously at
 *    0x7C00 before entering stage 1, so the kernel is already in
 *    memory at 0x7C00 + 0x8000 and no INT 13h happens at all: the
 *    Makefile keeps ocbl_kernel_sectors at its link-time zero, which
 *    makes stage 2a skip its read loop entirely. */
#ifndef OCBL_BOOT_CD
#define CORE_BOOT_BIOS_KERNEL_BASE  0x00020000
#else
#define CORE_BOOT_BIOS_KERNEL_BASE  0x0000FC00
#endif

/* link/load address of the 64-bit payload (stage 2b + C code):
 * inside the first 64 KiB segment (stage 1 loads stages 2a+2b with
 * ONE extended read of 64 sectors into 0x7E00..0xFE00) */
#define CORE_BOOT_BIOS_64_ENTRY     0x0000BC00

/* 64-bit stack top for the C payload (kernel area stops at 256 KiB) */
#define CORE_BOOT_BIOS_STACK_TOP    0x00080000

/* maximum kernel.elf size the disk image reserves: 512 sectors */
#define CORE_BOOT_BIOS_KERNEL_MAX_SECTORS 512

#endif /* CORE_BOOT_BIOS_LAYOUT_H */
