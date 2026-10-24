/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS — WP-01
 * File: linker.ld
 * Purpose: Linker script for the L0 kernel.
 *
 * Layout:
 *   - The kernel is loaded at physical address 0x100000 (1 MiB) by GRUB.
 *   - The Multiboot2 header must be in the first 32768 bytes of the
 *     file, so we put .rodata.mbthdr at the very start of the loadable
 *     segment, immediately after the ELF header.
 *   - We use 4 KiB page alignment for all sections so the page tables
 *     (set up in boot.S) can later mark sections RO/NX cleanly.
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_LMA = 0x00100000;           /* load at 1 MiB */

/* Two PT_LOAD segments: one RX for code+rodata, one RW for data+bss.
 * This avoids the "LOAD segment with RWX permissions" warning. */
PHDRS
{
    boot PT_LOAD FLAGS(5);          /* R+X */
    data PT_LOAD FLAGS(6);          /* R+W */
}

SECTIONS
{
    . = KERNEL_LMA;

    .mbthdr : {
        KEEP(*(.rodata.mbthdr))
    } :boot

    .text : {
        /* WP-11-crash_recovery: the TUI code window. Every .text byte
         * of build/tui/*.o and build/chinese/tui_cn_*.o lands between
         * __tui_text_start and __tui_text_end so the exception
         * dispatcher can decide "this ring-0 fault happened INSIDE a
         * TUI page" and route it to the tui_dt_guard() longjmp fence
         * instead of a full kernel panic. The glob matches the
         * absolute object paths the Makefile feeds to ld. */
        __tui_text_start = .;
        */tui/tui_*.o(.text .text.*)
        */tui_cn_*.o(.text .text.*)
        __tui_text_end = .;

        *(.text .text.*)
    } :boot

    .rodata : {
        *(.rodata .rodata.*)
    } :boot

    /* Open Cube OS 2.0.0 - file-local identity declaration sections.
     * One entry per OC_SHELL_CMD / OC_DRIVER_DECL / OC_SERVICE_DECL /
     * OC_L1_APP_DECL macro (see kernel/core/core_registry.h).  Boot-time
     * collection walks these boundaries; adding a command/driver/
     * service/app means adding a file - never editing a global table. */
    /* v2.0.0 note: gcc emits each declaration object's input section
     * with a 32-byte alignment hint; the 40-byte oc_service_decl_t /
     * oc_l1_app_decl_t entries would then get 24 bytes of inter-object
     * padding (40 is not a multiple of 32) and the registry walk -
     * which steps by sizeof(decl) - would read misaligned garbage.
     * SUBALIGN(8) forces 8-byte input-section granularity: every
     * declaration type in this tree is a multiple of 8 bytes, so the
     * entries pack back-to-back with no padding. */
    .oc_cmds : SUBALIGN(8) {
        __oc_cmds_start = .;
        KEEP(*(.oc_cmds))
        __oc_cmds_end = .;
    } :boot
    .oc_drvs : SUBALIGN(8) {
        __oc_drvs_start = .;
        KEEP(*(.oc_drvs))
        __oc_drvs_end = .;
    } :boot
    .oc_svcs : SUBALIGN(8) {
        __oc_svcs_start = .;
        KEEP(*(.oc_svcs))
        __oc_svcs_end = .;
    } :boot
    .oc_apps : SUBALIGN(8) {
        __oc_apps_start = .;
        KEEP(*(.oc_apps))
        __oc_apps_end = .;
    } :boot

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /* Symbol marking the end of the kernel image. Used by PMM to reserve
     * the kernel's physical pages. */
    __end_of_kernel = .;

    /* WP-10-wp08fix1 guard rail: user address spaces split the
     * 0x400000-0x600000 window into an empty page table (P3-1 security
     * fix), so kernel .bss/data above 0x400000 is unreachable from
     * syscall/IRQ context (kernel code running on a user CR3). The
     * kernel image must therefore end below 0x400000 - enforce it at
     * link time so a future build fails loudly instead of #PF'ing at
     * boot. */
    ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the 0x400000 identity-window boundary - move large buffers to kmalloc")

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
