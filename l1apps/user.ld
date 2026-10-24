/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 *
 * user.ld - default link script for user programs (asm + C).
 * Load address: 0x400000. Sections: .text, .rodata, .data, .bss.
 */
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data : { *(.data) *(.data.*) }
    .bss : { *(.bss) *(.bss.*) *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
