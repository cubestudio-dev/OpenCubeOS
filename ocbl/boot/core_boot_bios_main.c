/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL BIOS 64-bit C entry point (WP-02-ocbl).
 *
 * Runs in 64-bit long mode with the first 1 GiB identity-mapped,
 * right after stage 2b handed over. Brings up the two debug outputs
 * (COM1 serial + VGA text), parses and loads the kernel ELF that
 * stage 2a streamed to CORE_BOOT_BIOS_KERNEL_BASE, and jumps to the
 * kernel entry.
 *
 * The boot arguments are zeroed: the NAK kernel's entry currently
 * ignores its Multiboot2 arguments (WP-01 contract), and a proper
 * boot-protocol parameter block for a GRUB-less BIOS start is the
 * job of a later work package. Declared honestly here and in the
 * delivery report.
 *
 * Naming: [class]_[category]_[role] - core (boot core), boot
 * (bootloader), bios_main (what it is).
 */

#include <stdint.h>
#include <stddef.h>

#include "boot/core_boot_bios_layout.h"
#include "boot/core_boot_elf_loader.h"
#include "drivers/drv_serial_16550.h"
#include "drivers/drv_video_vga.h"

void core_boot_bios_main(uint64_t kernel_base);

static void bios_puts(const char *s)
{
    drv_serial_puts(s);
    drv_video_puts(s);
}

/* Minimal unsigned-to-hex printer (no libc). */
static void bios_puthex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[19];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; ++i) {
        buf[2 + i] = digits[(value >> (60 - 4 * i)) & 0xFU];
    }
    buf[18] = '\0';
    bios_puts(buf);
}

void core_boot_bios_main(uint64_t kernel_base)
{
    uint64_t entry;

    drv_serial_init();
    drv_video_init();

    bios_puts("OCBL: BIOS boot, long mode, serial+VGA up\n");
    bios_puts("OCBL: kernel image at ");
    bios_puthex(kernel_base);
    bios_puts(", parsing ELF64\n");

    entry = core_boot_elf_load((const void *)(uintptr_t)kernel_base);
    if (entry == 0U) {
        bios_puts("OCBL: ERROR - kernel.elf failed to validate\n");
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    bios_puts("OCBL: kernel entry at ");
    bios_puthex(entry);
    bios_puts(", jumping\n");

    ((void (*)(uint32_t, uint32_t))(uintptr_t)entry)(0U, 0U);

    /* the kernel never returns */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
