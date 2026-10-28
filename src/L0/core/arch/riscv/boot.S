# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-13-build_system
# File: arch/riscv/boot.S
# Purpose: RISC-V 64 bring-up entry.  QEMU's `-machine virt -kernel <elf>`
# loads this image at 0x80000000 with the MMU off in M-mode (QEMU boots
# raw rv64 kernels directly; OpenSBI is bypassed because -kernel with an
# ELF whose entry is _start gets hart 0 straight into our code).
#
# The entry point:
#   1. parks every hart except hart 0 (mhartid),
#   2. zeroes the .bss section,
#   3. installs the sp stack from the linker symbol,
#   4. calls the C entry (kernel_main).
#

.section .text.boot, "ax"

.global _entry
_entry:
    /* park all harts except hart 0 (mhartid, M-mode CSRs are readable
     * here because QEMU -kernel enters in M-mode) */
    csrr    t0, mhartid
    bnez    t0, park

    /* zero .bss: __bss_start .. __bss_end */
    la      t1, __bss_start
    la      t2, __bss_end
1:
    bgeu    t1, t2, 2f
    sd      zero, 0(t1)
    addi    t1, t1, 8
    j       1b
2:
    /* stack: full-descending, top of the .stack section */
    la      t1, __stack_top
    mv      sp, t1

    call    kernel_main

park:
    wfi
    j       park
