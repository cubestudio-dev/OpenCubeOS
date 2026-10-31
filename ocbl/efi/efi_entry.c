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
 *
 *          B5-5 scope: the payload handoff - GOP framebuffer, the
 *          GetMemoryMap->E820 conversion (ocbl_mmap_efi.c), the ELF
 *          loads through the shared loader, ExitBootServices with the
 *          retry loop, and the 64->32 downgrade through the
 *          low-address trampoline (handoff_blob.asm, copied to the
 *          reserved 0x30000 bounce window right after EBS).
 */
#include "efi.h"
#include "../common/ocbl_serial.h"
#include "../common/ocbl_scan.h"
#include "../common/ocbl_disk.h"
#include "../common/ocbl_menu.h"
#include "../common/ocbl_boot.h"
#include "../common/ocbl_wdt.h"
#include "../common/ocbl_fs.h"
#include "../common/ocbl_fb.h"
#include "../common/ocbl_mmap.h"

/* efi/handoff_data.asm (incbin of handoff_blob.bin) */
extern char ocbl_efi_handoff_blob[];
extern char ocbl_efi_handoff_blob_end[];

/* the PE converter leaves the .bss range as raw-vs-virtual slack;
 * EDK2's loader does NOT guarantee zero-fill there (live-caught:
 * g_ndisks came up as 2.9 billion), so the entry zeroes it first -
 * the standard freestanding-EFI crt0 move. */
extern char __bss_start[];
extern char __bss_end[];

/* efi/ocbl_disk_efi.c */
void ocbl_efi_disk_init(void);

/* efi/ocbl_chain_efi.c (B5-6: LoadImage/StartImage chainload) */
efi_status ocbl_efi_chainload(const struct ocbl_volume* v,
                              const char* efi_path);

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

    /* B5-4: the menu (same state machine as the BIOS flavor - the
     * ConOut render + ReadKeyStroke/16550 keys + timer-event clock
     * are the backend in ocbl_con_efi.c) */
    ocbl_wdt_probe();
    if (ocbl_wdt_present())
        ocbl_wdt_arm(60);
    u32 pick = ocbl_menu_run();
    ocbl_menu_announce(pick);


    /* B5-5: the payload handoff (GOP fb, GetMemoryMap, ELF load,
     * ExitBootServices, the 64->32 downgrade through the low-address
     * trampoline).  B5-6 wires the LoadImage chainload; until then
     * every foreign pick parks honestly - except the Shutdown row,
     * which is ALREADY the real ACPI S5 (the FADT/_S5 walk in common
     * + the RSDP from the config table). */
    ocbl_scan_t* t = ocbl_scan_result();
    if (pick >= t->count) {
        ocbl_serial_puts("[boot] ACPI S5 shutdown\r\n");
        ocbl_wdt_disarm();
        ocbl_acpi_enter_s5();
        ocbl_serial_puts("[boot] S5 did not power off; halting\r\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    ocbl_entry_t* e = &t->entries[pick];
    if (e->type != OCBL_ENT_OCOS && e->type != OCBL_ENT_OCIRE) {
        /* B5-6: the ESP chainload (Windows bootmgfw / a foreign Linux
         * shim / a generic BOOTX64.EFI) - the UEFI mirror of the
         * BIOS flavor's VBR chainload. */
        ocbl_volume_t cv;
        if (!ocbl_fs_mount(e->drive, e->base_lba, e->ssz, &cv)) {
            ocbl_serial_puts("[boot] FATAL: chainload volume remount "
                             "failed\r\n");
            for (;;)
                __asm__ volatile("hlt");
        }
        ocbl_serial_puts("[boot] EFI chainload\r\n");
        ocbl_wdt_disarm();
        ocbl_efi_chainload(&cv, e->efi_path);
        /* the image returned control: park honestly (menu re-entry is
         * out of 3.0.0 scope - a chainloaded loader that exits hands
         * the machine back, the machine halts rather than guessing) */
        for (;;)
            __asm__ volatile("hlt");
    }

    /* ---- B5-5: the mb2 payload path (the ocbl_run() flow, with the
     * three UEFI substitutions: GOP for VBE, GetMemoryMap for E820,
     * ExitBootServices + the 0x30000 trampoline for the in-place
     * downgrade) ---- */
    ocbl_volume_t v;
    if (!ocbl_fs_mount(e->drive, e->base_lba, e->ssz, &v)) {
        ocbl_serial_puts("[boot] FATAL: payload volume remount "
                         "failed\r\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    const char* slot = 0;
    if (e->type == OCBL_ENT_OCOS)
        slot = e->slot == 1 ? "A" : (e->slot == 2 ? "B" : "ISO");

    /* the mb2 "self" module: raw kernel ELF copy at 0x800000 (only
     * OCOS entries; install/abdisk copy it from there) */
    u32 self_size = 0;
    if (e->type == OCBL_ENT_OCOS)
        self_size = ocbl_load_raw(&v, e->path, OCBL_SELF_BASE);

    ocbl_fb_info_t fb;
    bool fb_ok = ocbl_gop_set_fb(&fb);
    if (!fb_ok)
        ocbl_serial_puts("[boot] GOP FAILED (kernel will refuse)\r\n");

    u64 base = e->type == OCBL_ENT_OCIRE ? OCBL_OCIRE_BASE : 0x100000ull;
    u32 ksize = 0;
    u64 entry = ocbl_load_elf(&v, e->path, base, &ksize);
    if (!entry) {
        ocbl_serial_puts("[boot] FATAL: kernel load failed\r\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    ocbl_serial_puts("[boot] kernel loaded: entry=0x");
    ocbl_serial_hex(entry, 8);
    ocbl_serial_puts(" size=");
    ocbl_serial_dec(ksize);
    ocbl_serial_puts("\r\n");

    u64 self_start = self_size ? OCBL_SELF_BASE : 0;
    u64 self_end = self_start + self_size;


    /* the firmware watchdog (OVMF arms 5 minutes) would reset the
     * machine right under the kernel - off BEFORE the EBS loop so it
     * can never invalidate the map key */
    ocbl_efi_bs->set_watchdog_timer(0, 0, 0, 0);

    /* our hardware watchdog instead: fresh 60 s window for the payload
     * to install its own heartbeat */
    ocbl_wdt_arm(60);

    /* ExitBootServices loop (spec 7.2): the map key expires with any
     * boot-services allocation, so query -> build-mbi -> EBS runs as
     * one unit and retries honestly on EFI_INVALID_PARAMETER. */
    int exited = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        u32 nmap = ocbl_mmap_query();
        if (!nmap) {
            ocbl_serial_puts("[efi] FATAL: memory map failed\r\n");
            for (;;)
                __asm__ volatile("hlt");
        }
        u32 mbisz = ocbl_build_mbi(slot, self_start, self_end,
                                   fb_ok ? &fb : 0);
        ocbl_serial_puts("[boot] mbi built: ");
        ocbl_serial_dec(mbisz);
        ocbl_serial_puts(" bytes at 0x50000 (");
        ocbl_serial_dec(nmap);
        ocbl_serial_puts(" mmap ranges)\r\n");
        efi_status st = ocbl_efi_bs->exit_boot_services(
            ocbl_efi_image, ocbl_mmap_boot_key());
        if (st == EFI_SUCCESS) {
            exited = 1;
            break;
        }
        ocbl_serial_puts("[efi] EBS retry (0x");
        ocbl_serial_hex((u64)st, 8);
        ocbl_serial_puts(") - map key expired\r\n");
    }
    if (!exited) {
        ocbl_serial_puts("[boot] FATAL: ExitBootServices refused\r\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    ocbl_serial_puts("[boot] boot services exited\r\n");

    /* copy the downgrade trampoline into the reserved bounce window
     * and drop into it - plain memory writes only (0x30000 survives
     * both the firmware page tables and the CR0.PG=0 world). */
    {
        const char* b0 = ocbl_efi_handoff_blob;
        const char* b1 = ocbl_efi_handoff_blob_end;
        u8* dst = (u8*)(u64)OCBL_BOUNCE;
        u64 n = (u64)(b1 - b0);
        for (u64 i = 0; i < n; i++)
            dst[i] = (u8)b0[i];
        ocbl_serial_puts("[boot] handoff trampoline at 0x30000 (");
        ocbl_serial_dec((u32)n);
        ocbl_serial_puts(" bytes)\r\n");
        ((void (*)(u64, u64))(u64)OCBL_BOUNCE)(entry, OCBL_MBI_BASE);
    }

    for (;;)
        __asm__ volatile("hlt");
}
