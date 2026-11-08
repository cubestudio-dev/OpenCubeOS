/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL BIOS stage 2b, the 64-bit payload entry
 * (WP-02-ocbl).
 *
 * The long-mode far jump from stage 2a lands here (link address:
 * CORE_BOOT_BIOS_64_ENTRY = 0x10000, inside the identity map). This
 * small 64-bit stub resets the segment registers, sets up the C
 * stack and hands control to core_boot_bios_main() with the kernel
 * image base as the first argument. It never returns; if the C code
 * ever does, the machine halts.
 *
 * Naming: [class]_[category]_[role] - core (boot core), boot
 * (bootloader), bios_stage2b (BIOS second stage, 64-bit part).
 */

#include "boot/core_boot_bios_layout.h"

.text
.code64
.globl core_boot_bios_stage2b_entry
.type core_boot_bios_stage2b_entry, @function
core_boot_bios_stage2b_entry:
    movw $0x10, %ax                    /* 32-bit data selector from GDT */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    movq $CORE_BOOT_BIOS_STACK_TOP, %rsp
    movq $CORE_BOOT_BIOS_KERNEL_BASE, %rdi   /* arg 1: kernel image base */
    call core_boot_bios_main

    /* core_boot_bios_main never returns; halt forever if it ever does */
    cli
.Lspin:
    hlt
    jmp .Lspin
.size core_boot_bios_stage2b_entry, . - core_boot_bios_stage2b_entry
