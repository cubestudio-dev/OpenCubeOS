/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/linker_ocire.ld
 * Purpose: Linker script for the OCIRE independent recovery kernel.
 *
 * OCIRE is deliberately an independent kernel: this script shares no
 * symbols or layout with the OCOS linker.ld.
 *
 * WP-02: the load address moved from 1 MiB to 16 MiB.  The boot
 * supervisor (kexec) loads the OCOS kernel into ITS natural 1 MiB
 * range while OCIRE is still running - at 1 MiB that copy would
 * overwrite the executing OCIRE code; at 16 MiB the two kernels are
 * disjoint and the hand-over is a plain segment copy + mode switch.
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

OCIRE_LMA = 0x01000000;           /* load at 16 MiB (see header note) */

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
