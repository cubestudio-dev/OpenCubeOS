/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/linker_efi.ld
 * Purpose: layout script for the UEFI flavor's INTERMEDIATE ELF
 *          (linked with `ld -m elf_x86_64 --emit-relocs`, then
 *          converted to PE by ocbl/elf_to_efi.py).
 *
 *          The VMA base 0x11000 is the PE load address of the first
 *          section (ImageBase 0x10000 + RVA 0x1000), so ELF VMAs,
 *          PE RVAs and the runtime layout all line up one-to-one:
 *          every link-time absolute value equals its final virtual
 *          address, and the converter's DIR64 deltas stay correct
 *          for any load address (live-caught: a 0x10000 base left
 *          every absolute reference off by exactly 0x1000 - the
 *          difference between the ELF start and the first PE section
 *          RVA - which made even a zero-delta load read garbage).
 */
ENTRY(efi_main)

SECTIONS
{
    . = 0x11000;

    /* page-aligned so the PE can mirror the ELF layout 1:1
     * (RVA = VMA - ImageBase, zero re-placement - the B5-3 final
     * architecture; see elf_to_efi.py) */
    .text : ALIGN(0x1000) {
        *(.text .text.*)
    }

    .rodata : ALIGN(0x1000) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(0x1000) {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(0x1000) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame .eh_frame_hdr)
        *(.note .note.*)
    }
}
