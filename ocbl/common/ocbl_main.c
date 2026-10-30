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
#include "ocbl_bios.h"
#include "ocbl_disk.h"
#include "ocbl_mmap.h"
#include "ocbl_scan.h"

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

#ifdef OCBL_FLAVOR_BIOS
static void bios_b2a_probe(void)
{
    /* trampoline live check + E820 + disk sweep */
    ocbl_tramp_init();
    ocbl_serial_puts("[b2a] trampoline installed at 0x40000\r\n");

    u32 n = ocbl_mmap_query();
    ocbl_serial_puts("[b2a] E820 entries: ");
    ocbl_serial_dec(n);
    ocbl_serial_puts("\r\n");
    const ocbl_mmap_t* mm = ocbl_mmap_get();
    u32 shown = 0;
    for (u32 i = 0; i < n && shown < 8; i++) {
        if (mm->entries[i].len == 0)
            continue;                       /* the E820 terminator entry */
        shown++;
        ocbl_serial_puts("  [");
        ocbl_serial_dec(i);
        ocbl_serial_puts("] base=0x");
        ocbl_serial_hex(mm->entries[i].addr, 8);
        ocbl_serial_puts(" len=0x");
        ocbl_serial_hex(mm->entries[i].len, 8);
        ocbl_serial_puts(" type=");
        ocbl_serial_dec(mm->entries[i].type);
        ocbl_serial_puts("\r\n");
    }

    /* B2a read verification via the real driver path:
     *   hd 0x80 LBA 0   -> the control disk magic EB..CTRLHD
     *   cd 0xE0 LBA 16  -> the ISO PVD, 'CD001' at bytes 1..5
     *   cd 0xE0 LBA 34  -> our own El Torito image (stage1 bytes)
     */
    {
        static u8 buf[2048];
        if (ocbl_disk_geom(0x80, 0, 0) && ocbl_disk_read(0x80, 0, 1, buf)) {
            ocbl_serial_puts("[b2a] hd 0x80 LBA0:");
            for (int i = 0; i < 8; i++)
                ocbl_serial_hex(buf[i], 2);
            ocbl_serial_puts(" sig=");
            ocbl_serial_hex((u16)(buf[510] | (buf[511] << 8)), 4);
            ocbl_serial_puts("\r\n");
        }
    }
    u8 boot_drive = ocbl_peek8(OCBL_LO_BOOTDRIVE);
    ocbl_serial_puts("[b2a] scanning BIOS drives (boot=0x");
    ocbl_serial_hex(boot_drive, 2);
    ocbl_serial_puts(")\r\n");
    for (u8 d = 0x80; d < 0x90; d++) {
        u64 total = 0;
        u16 bps = 0;
        if (ocbl_disk_geom(d, &total, &bps)) {
            ocbl_serial_puts("  drive 0x");
            ocbl_serial_hex(d, 2);
            ocbl_serial_puts(": ");
            ocbl_serial_dec(total);
            ocbl_serial_puts(" sectors x ");
            ocbl_serial_dec(bps);
            ocbl_serial_puts(" B\r\n");
        }
    }
    /* the CD boot drive is usually 0xE0+ when booted from El Torito */
    if (boot_drive >= 0xE0) {
        u64 total = 0;
        u16 bps = 0;
        if (ocbl_disk_geom(boot_drive, &total, &bps)) {
            ocbl_serial_puts("  cdrom 0x");
            ocbl_serial_hex(boot_drive, 2);
            ocbl_serial_puts(": ");
            ocbl_serial_dec(total);
            ocbl_serial_puts(" sectors x ");
            ocbl_serial_dec(bps);
            ocbl_serial_puts(" B\r\n");
            /* the no-emulation drive is the real ATAPI CD: LBA units
             * are 2048-byte ISO sectors (verified live: LBA 34 = our
             * own boot image, LBA 16 = the PVD). */
            static u8 pvd[2048];
            if (ocbl_disk_read(boot_drive, 16, 1, pvd)) {
                ocbl_serial_puts("  PVD: '");
                for (int i = 1; i <= 5; i++)
                    ocbl_serial_putc((char)pvd[i]);
                ocbl_serial_puts("' type=");
                ocbl_serial_dec(pvd[0]);
                ocbl_serial_puts("\r\n");
            }
            if (ocbl_disk_read(boot_drive, 34, 1, pvd)) {
                ocbl_serial_puts("  boot image first bytes:");
                for (int i = 0; i < 8; i++) {
                    ocbl_serial_putc(' ');
                    ocbl_serial_hex(pvd[i], 2);
                }
                ocbl_serial_puts("\r\n");
            }
        }
    }
}
#endif

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

#ifdef OCBL_FLAVOR_BIOS
    bios_b2a_probe();
#endif

    /* B2b: the six-class boot-entry scan + serial dump (the menu
     * replaces this output in B3) */
    u32 n = ocbl_scan_all();
    (void)n;
    ocbl_scan_dump();

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
