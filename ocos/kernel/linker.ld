/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - kernel link script (WP-01-tree).
 *
 * The image is loaded by GRUB via Multiboot2:
 *   - the entry point is _start (kernel/core_boot_multiboot2.S);
 *   - the Multiboot2 header section is placed first so it stays well
 *     inside the first 32 KiB of the image;
 *   - everything is linked at 1 MiB with virtual == physical, which
 *     matches the identity mapping the boot trampoline installs.
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    .multiboot2_header : ALIGN(8)
    {
        KEEP(*(.multiboot2_header))
    }

    /* GRUB-less boot protocol: absolute 64-bit entry pointer (read by
     * OCBL from the loaded image, see core_boot_multiboot2.S) */
    .ocbl_boot64 : ALIGN(8)
    {
        KEEP(*(.ocbl_boot64))
    }

    .text : ALIGN(4096)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}
