/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-13-build_system
 * File: arch/arm64/boot.S
 * Purpose: ARM64 bring-up entry.  QEMU's `-machine virt -kernel <elf>`
 * loads this image at 0x40000000 with the CPU in EL1 and the MMU off
 * (QEMU boots raw kernels directly; no firmware loader needed).
 *
 * The entry point:
 *   1. parks secondary harts (QEMU -smp presents all harts at 0x40000000;
 *      only hart 0 may own the C stack),
 *   2. zeroes the .bss section,
 *   3. installs the stack pointer from the linker symbol,
 *   4. calls the C entry (kernel_main) with the dtb pointer in x0.
 */

.section .text.boot, "ax"

.global _entry
_entry:
    /* MPIDR_EL1: park every hart except hart 0 (affinity level 0). */
    mrs     x1, mpidr_el1
    ands    x1, x1, #0xff
    b.eq    1f
    wfe
    b       .

1:
    /* zero .bss: __bss_start .. __bss_end from the linker script */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
2:
    cmp     x1, x2
    b.hs    3f
    str     xzr, [x1], #8
    b       2b
3:
    /* stack: full-descending, top of the .stack section */
    ldr     x1, =__stack_top
    mov     sp, x1

    /* dtb pointer arrives in x0 (QEMU passes it for -kernel images);
     * forward it to the C entry as the one argument. */
    bl      kernel_main

    /* kernel_main never returns; if it ever does, park. */
    wfe
    b       .
