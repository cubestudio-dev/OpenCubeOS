/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-13-build_system
 * File: arch/riscv/linker.ld
 * Purpose: link script for the RISC-V 64 bring-up image.  QEMU's virt
 * board loads -kernel ELFs at 0x80000000; the entry symbol must be
 * first in the image so hart 0 lands on _entry.
 */

ENTRY(_entry)

SECTIONS
{
    . = 0x80000000;

    .text : {
        KEEP(*(.text.boot))     /* _entry must be the first byte */
        *(.text*)
    }

    .rodata : ALIGN(16) {
        *(.rodata*)
    }

    .data : ALIGN(16) {
        *(.data*)
        /* gp (global pointer) relaxation anchor: RISC-V ld relaxes
         * symbol loads against __global_pointer$ within +-2 KiB. */
        __global_pointer$ = . + 0x800;
    }

    .bss : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    . += 0x8000;                  /* 32 KiB stack for the C entry */
    __stack_top = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
