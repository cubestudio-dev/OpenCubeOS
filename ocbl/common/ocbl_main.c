/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_main.c
 * Purpose: OCBL 64-bit C entry (BIOS flavor arrives here from
 *          core_head.S; the UEFI flavor has its own efi_entry.c that
 *          eventually calls ocbl_run() too).
 *
 *          B1 scope: bring-up diagnostics - banner, CPU brand probe,
 *          flavor/drive facts, stack pointer - proving the whole
 *          16->32->64 chain landed correctly.  The scan engine, menu
 *          and boot handoff grow in here batch by batch.
 */
#include "ocbl_serial.h"

#ifndef OCBL_RELEASE
#define OCBL_RELEASE "3.0.0-WP05"
#endif
#ifndef OCBL_BUILD
#define OCBL_BUILD 0
#endif

static const char* flavor_name(void)
{
#ifdef OCBL_FLAVOR_BIOS
    u8 f = ocbl_peek8(OCBL_LO_FLAVOR);
    return f ? "mbr" : "el-torito";
#else
    return "uefi";
#endif
}

static void cpu_brand(char out[49])
{
    u32* p = (u32*)out;
    __asm__ volatile(
        "cpuid"
        : "=a"(p[0]), "=b"(p[1]), "=c"(p[2]), "=d"(p[3])
        : "a"(0x80000002));
    __asm__ volatile(
        "cpuid"
        : "=a"(p[4]), "=b"(p[5]), "=c"(p[6]), "=d"(p[7])
        : "a"(0x80000003));
    __asm__ volatile(
        "cpuid"
        : "=a"(p[8]), "=b"(p[9]), "=c"(p[10]), "=d"(p[11])
        : "a"(0x80000004));
    out[48] = 0;
}

void ocbl_run(void)
{
    char brand[64];
    u64 sp;

    ocbl_serial_init();
    ocbl_serial_puts("\r\nOCBL 3.0.0 boot loader (" OCBL_RELEASE " build ");
    ocbl_serial_dec(OCBL_BUILD);
    ocbl_serial_puts(")\r\n");
    ocbl_serial_puts("long mode 64-bit C entry OK\r\n");
    ocbl_serial_puts("flavor: ");
    ocbl_serial_puts(flavor_name());
#ifdef OCBL_FLAVOR_BIOS
    ocbl_serial_puts("  boot drive: 0x");
    ocbl_serial_hex(ocbl_peek8(OCBL_LO_BOOTDRIVE), 2);
    ocbl_serial_puts("  stage1 CS: 0x");
    ocbl_serial_hex(ocbl_peek16(0x522), 4);
#endif
    ocbl_serial_puts("\r\n");

    brand[0] = 0;
    cpu_brand(brand);
    ocbl_serial_puts("cpu: ");
    ocbl_serial_puts(brand);
    ocbl_serial_puts("\r\n");

    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    ocbl_serial_puts("stack at 0x");
    ocbl_serial_hex(sp, 8);
    ocbl_serial_puts("\r\n");

    ocbl_serial_puts("OCBL B1 chain verified\r\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
