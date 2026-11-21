/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocrmecl/linker_efi.ld
 * Purpose: layout script for the OCRMECL EFI track's INTERMEDIATE
 *          ELF (linked with `ld -m elf_x86_64 --emit-relocs`, then
 *          converted to PE by ocbl/elf_to_efi.py).  Same discipline
 *          as ocbl/linker_efi.ld: VMA base 0x11000 = ImageBase 0x10000
 *          + first-section RVA 0x1000, so ELF VMAs, PE RVAs and the
 *          runtime layout line up one-to-one for any load address.
 */
ENTRY(ocr_efi_main)

SECTIONS
{
    . = 0x11000;

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
