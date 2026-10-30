/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/efi_entry.c
 * Purpose: UEFI flavor entry point.  The firmware loads this image as
 *          a PE/COFF x64 application and calls efi_main with the MS
 *          x64 ABI; from here the flavor grows the same stages the
 *          BIOS flavor has: banner -> disk sweep -> scan -> menu ->
 *          payload handoff (the batch plan B5-1..B5-7).
 *
 *          B5-1 scope: prove the entry contract live (banner through
 *          BOTH the serial COM1 line - which is plain port I/O and
 *          works under UEFI exactly as under BIOS - and the firmware
 *          ConOut, which OVMF mirrors to the same serial line), print
 *          the firmware revision + config-table count, then park.
 *
 *          B5-3 scope: reserve the shared fixed low-memory windows
 *          (the BIOS flavor owns them by real-mode construction; the
 *          UEFI flavor must ASK the firmware with AllocatePages at
 *          the exact addresses so the common fs/loader code keeps its
 *          hardcoded layout and the kernel mb2 contract stays frozen),
 *          enumerate BlockIo disks and run the B2b six-class scanner
 *          on top of them.
 */
#include "efi.h"
#include "../common/ocbl_serial.h"
#include "../common/ocbl_scan.h"
#include "../common/ocbl_disk.h"

/* the PE converter leaves the .bss range as raw-vs-virtual slack;
 * EDK2's loader does NOT guarantee zero-fill there (live-caught:
 * g_ndisks came up as 2.9 billion), so the entry zeroes it first -
 * the standard freestanding-EFI crt0 move. */
extern char __bss_start[];
extern char __bss_end[];

/* efi/ocbl_disk_efi.c */
void ocbl_efi_disk_init(void);

#ifndef OCBL_RELEASE
#define OCBL_RELEASE "3.0.0-WP05"
#endif
#ifndef OCBL_BUILD
#define OCBL_BUILD 0
#endif

/* ---- globals the whole flavor shares (declared in efi.h) ---- */
efi_handle ocbl_efi_image;
efi_system_table* ocbl_efi_st;
efi_boot_services* ocbl_efi_bs;

/* ---- GUID constants (UEFI spec appendix A) ---- */
const efi_guid ocbl_efi_block_io_guid = {
    /* live-caught: 964e5b21 is BlockIo - the 964e5b22 next door is
     * SimpleFileSystem (the SFS/BlockIo GUID pair differ in ONE hex
     * digit; asking for ...22 returned every FAT volume's SFS and the
     * "BlockIo" dump was a SimpleFileSystem all along) */
    0x964e5b21, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
const efi_guid ocbl_efi_gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };
const efi_guid ocbl_efi_loaded_image_guid = {
    0x5b1b31a1, 0x9562, 0x11d2,
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
const efi_guid ocbl_efi_acpi20_guid = {
    0x8868e871, 0xe4f1, 0x11d3,
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
const efi_guid ocbl_efi_acpi10_guid = {
    0xeb9d2d30, 0x2d88, 0x11d3,
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };

/* ---- fixed-window reservation (the B5-2 platform decision) ----
 * The common layers address these windows by absolute physical
 * address (0x30000 bounce / 0x50000 MBI / 0x60000 mmap + 0x62000
 * sector window + 0x62800 128 KiB FAT cache).  Under UEFI the
 * firmware owns the memory map, so we take the pages with
 * EFI_ALLOCATE_ADDRESS - keeping the layout IDENTICAL between the
 * flavors (zero changes in common code, the kernel handoff contract
 * untouched).  A failure here is fatal and reported honestly. */
static int reserve(u64 addr, u64 bytes)
{
    efi_phys a = addr;
    efi_status st = ocbl_efi_bs->allocate_pages(
        2 /* EFI_ALLOCATE_ADDRESS */, 5 /* BOOT_SERVICES_DATA */,
        (bytes + 0xFFF) >> 12, &a);
    return st == EFI_SUCCESS;
}

/* ---- tiny helpers ---- */

/* firmware ConOut line (UTF-16): mirrors to the QEMU serial console
 * through OVMF; used for the menu render backend later (B5-4) */
void ocbl_efi_conout(const char* s)
{
    static efi_char16 w[160];
    u32 i = 0;
    while (s[i] && i + 1 < sizeof(w) / sizeof(w[0])) {
        w[i] = (efi_char16)(u8)s[i];
        i++;
    }
    w[i] = 0;
    if (ocbl_efi_st && ocbl_efi_st->con_out)
        ocbl_efi_st->con_out->output_string(ocbl_efi_st->con_out, w);
}

static void cpu_brand(char out[49])
{
    u32* p = (u32*)out;
    __asm__ volatile("cpuid"
                     : "=a"(p[0]), "=b"(p[1]), "=c"(p[2]), "=d"(p[3])
                     : "a"(0x80000002));
    __asm__ volatile("cpuid"
                     : "=a"(p[4]), "=b"(p[5]), "=c"(p[6]), "=d"(p[7])
                     : "a"(0x80000003));
    __asm__ volatile("cpuid"
                     : "=a"(p[8]), "=b"(p[9]), "=c"(p[10]), "=d"(p[11])
                     : "a"(0x80000004));
    out[48] = 0;
}

/* ---- the entry point (MS x64 ABI via EFI_CALL) ---- */
EFI_CALL efi_status efi_main(efi_handle image, efi_system_table* st)
{
    char brand[64];
    u64 sp;

    ocbl_efi_image = image;
    ocbl_efi_st = st;
    ocbl_efi_bs = st->boot_services;

    ocbl_serial_init();
    ocbl_serial_puts("[efi] bss 0x");
    ocbl_serial_hex((u64)__bss_start, 12);
    ocbl_serial_puts("-0x");
    ocbl_serial_hex((u64)__bss_end, 12);
    ocbl_serial_puts("\r\n");
    for (char* p = __bss_start; p < __bss_end; p++)
        *p = 0;

    /* the plain COM1 driver works under UEFI as well (port I/O is
     * unchanged); this keeps ONE serial transcript format shared by
     * both flavors - the acceptance evidence stays comparable */
    ocbl_serial_init();
    ocbl_serial_puts("\r\nOCBL 3.0.0 boot loader (" OCBL_RELEASE " build ");
    ocbl_serial_dec(OCBL_BUILD);
    ocbl_serial_puts(")\r\n");
    ocbl_serial_puts("UEFI flavor entry OK\r\n");

    /* firmware facts */
    ocbl_serial_puts("[efi] UEFI revision 0x");
    ocbl_serial_hex(st->hdr.revision, 8);
    ocbl_serial_puts("  config tables: ");
    ocbl_serial_dec(st->nr_table_entries);
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

    ocbl_serial_puts("OCBL B5-1 UEFI chain verified\r\n");

    /* reserve the shared low-memory windows (B5-3): one honest gate -
     * if the firmware already uses any of them, the common loader
     * layout would corrupt firmware data, so we stop right here */
    if (!reserve(0x30000, 0x10000) ||   /* loader bounce window      */
        !reserve(0x50000, 0x10000) ||   /* multiboot2 info (64 KiB)  */
        !reserve(0x60000, 0x23000)) {   /* mmap + secbuf + FAT cache */
        ocbl_serial_puts("[efi] FATAL: fixed low-memory windows not "
                         "reservable\r\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    ocbl_serial_puts("[efi] fixed windows reserved (0x30000/0x50000/"
                     "0x60000)\r\n");

    /* the disk backend + the B2b six-class scanner (B5-3) */
    ocbl_efi_disk_init();
    u32 entries = ocbl_scan_all();
    ocbl_scan_dump();
    ocbl_serial_puts("[efi] scan complete: ");
    ocbl_serial_dec(entries);
    ocbl_serial_puts(" entries\r\n");

    /* mirror the banner on the firmware console too (OVMF routes
     * ConOut to the serial line as well - both surfaces stay in the
     * same transcript for the acceptance run) */
    ocbl_efi_conout("OCBL 3.0.0 boot loader - UEFI flavor - B5-1 OK\r\n");

    /* park: B5-4 wires the menu (ConOut render + ReadKeyStroke),
     * B5-5 the payload handoff, B5-6 the LoadImage chainload */
    for (;;)
        __asm__ volatile("hlt");
}
