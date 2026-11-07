/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/linker_bios.ld
 * Purpose: link the OCBL BIOS core at linear 0x8200 (stage1 sector is
 *          prepended at image build time; both flavors put the core
 *          there).  .bss is NOBITS - core_head.S zeros it at runtime.
 */
ENTRY(core_head)

PHDRS
{
    text PT_LOAD FLAGS(5);      /* R-X */
    data PT_LOAD FLAGS(6);      /* RW- */
}

SECTIONS
{
    . = 0x7E00;

    .text : {
        *(.text.head)
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    .data : {
        *(.data*)
    } :data

    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.stab*)
        *(.debug*)
    }
}
