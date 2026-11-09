/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
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
 *
 * WP-06c: a dedicated NOLOAD work zone (.ocsework) follows .bss. The
 * GUI wizard state (~21 KiB: widget tree, 9-step state machine, the
 * command-capture buffer) lives there instead of competing for the
 * regular .bss variable area - boot.o is linked last, so the boot
 * page tables + stack still close .bss, and the regular variable
 * area keeps the FULL room up to __boot_pml4. The OCBL mb2 ELF
 * loader zeroes [p_filesz, p_memsz) per segment (ocbl_load.c), so
 * NOLOAD sections arrive zero-filled without any file payload -
 * the image stays as small as before.
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

    /* WP-06c: GUI/wizard work zone - NOLOAD, zeroed by the loader
     * (p_memsz > p_filesz). Markers bracket it for tools/check_bss.py
     * (variables placed here are legal; the forbidden zone remains
     * [__boot_pml4, __ocse_work_start)). */
    . = ALIGN(16);
    __ocse_work_start = .;
    .ocsework (NOLOAD) : {
        *(.ocsework)
        *(.ocsework.*)
    } :data
    . = ALIGN(16);
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
