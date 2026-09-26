/* SPDX-License-Identifier: Apache-2.0 */
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
        *(.text .text.*)
    } :boot

    .rodata : {
        *(.rodata .rodata.*)
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

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
