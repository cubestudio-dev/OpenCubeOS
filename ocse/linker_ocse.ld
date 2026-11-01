/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/linker_ocse.ld
 * Purpose: Linker script for the OCSE install-environment kernel.
 *
 * Layout: identical contract to the v1.0.0 L0 linker.ld so the OCBL
 * mb2 ELF loader (WP-05 b4a handoff path) can load OCSE exactly like
 * it loads OCOS:
 *   - loaded at physical 1 MiB, single RX/RW segment pair,
 *   - Multiboot2 header first in the file (.rodata.mbthdr),
 *   - image must end below 0x400000 (identity-window boundary).
 * Differences: no TUI code window (no TUI in OCSE), no grub_boot_data
 * second stage (GRUB is gone from the release chain since B5-7).
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_LMA = 0x00100000;           /* load at 1 MiB */

PHDRS
{
    boot PT_LOAD FLAGS(5);          /* R+X */
    data PT_LOAD FLAGS(6);           /* R+W */
}

SECTIONS
{
    . = KERNEL_LMA;

    .mbthdr : {
        KEEP(*(.rodata.mbthdr))
    } :boot

    .text : {
        *(.text .text.*)
    } :boot

    .rodata : {
        *(.rodata .rodata.*)
    } :boot

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    __end_of_kernel = .;

    /* Same guard as the v1.0.0 L0: the boot page tables identity-map
     * the first 4 GiB but OCSE keeps working inside the low window the
     * OCBL handoff builds; fail the link loudly if the image grows
     * past 0x400000. */
    ASSERT(__end_of_kernel <= 0x400000, "ocse image grew past the 0x400000 identity-window boundary")

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
    }
}
