/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 *
 * ld_so.ld - link script for the ld.so test program (C version, Batch 3+).
 *
 * Combines .text, .rodata, .data, .bss into a single output .text section
 * so the kernel can map ld.so with one R+E PT_LOAD segment (matching
 * what the kernel's user_process_create does for hello.elf and the
 * Batch 2 asm version).
 *
 * Load address: 0x10000000 (256 MB mark — above USER_BRK_BASE 0x500000,
 * below USER_STACK_TOP 0x40000000, doesn't collide with the main
 * program mapped at 0x20000000 in Batch 3).
 */
ENTRY(_start)
SECTIONS {
    . = 0x10000000;
    .text : {
        *(.text)
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) *(.eh_frame_hdr) }
}
