/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/linker_ocire.ld
 * Purpose: Linker script for the OCIRE independent recovery kernel.
 *
 * OCIRE is deliberately an independent kernel: this script shares no
 * symbols or layout with the OCOS linker.ld.  The kernel is loaded by
 * GRUB (multiboot2) at physical address 1 MiB.
 *
 * Layout:
 *   - The multiboot2 header must be within the first 32768 bytes of
 *     the image; it sits in .rodata.mbthdr at the very start of the
 *     first loadable segment (immediately after the ELF header).
 *   - Two PT_LOAD segments: RX for code+rodata, RW for data+bss
 *     (avoids the "LOAD segment with RWX permissions" warning).
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

OCIRE_LMA = 0x00100000;           /* load at 1 MiB */

PHDRS
{
    boot PT_LOAD FLAGS(5);        /* R+X */
    data PT_LOAD FLAGS(6);        /* R+W */
}

SECTIONS
{
    . = OCIRE_LMA;

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
        __ocire_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __ocire_bss_end = .;
    } :data

    /* Symbols used by the boot asm / main C entry. */
    __ocire_text_start = ADDR(.mbthdr);
    __ocire_end = .;

    /DISCARD/ : {
        *(.eh_frame .eh_frame.*)
        *(.comment)
        *(.note .note.*)
    }
}
