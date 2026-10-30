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
#include "ocbl_menu.h"
#include "ocbl_boot.h"
#include "ocbl_vbe.h"
#include "ocbl_fs.h"
#include "ocbl_wdt.h"
#include "ocbl_chain.h"

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

    /* B2b: the six-class boot-entry scan + serial dump (diagnostics;
     * the menu below is the user-facing surface) */
    ocbl_scan_all();
    ocbl_scan_dump();

    /* B3: the menu - three keys, Shutdown always last, countdown to
     * the default entry */
    /* the hardware watchdog guards the whole OCBL stay: probe and
     * arm 60 s now, the menu loop kicks it as its heartbeat */
    ocbl_wdt_probe();
    if (ocbl_wdt_present())
        ocbl_wdt_arm(60);

    u32 pick = ocbl_menu_run();
    ocbl_menu_announce(pick);

    /* B4: load the chosen payload and hand off (multiboot2, the
     * exact GRUB contract); B4b wires Shutdown to the real S5 */
    ocbl_scan_t* t = ocbl_scan_result();
    if (pick >= t->count) {
        ocbl_serial_puts("[boot] ACPI S5 shutdown\r\n");
        ocbl_wdt_disarm();
        ocbl_acpi_enter_s5();
        ocbl_serial_puts("[boot] S5 did not power off; halting\r\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    }
    ocbl_entry_t* e = &t->entries[pick];
    if (e->type != OCBL_ENT_OCOS && e->type != OCBL_ENT_OCIRE) {
        /* BIOS chainload: the volume's own VBR takes over (GRUB
         * `chainloader +1` contract).  ESP-only loaders (BOOTX64.EFI)
         * are refused honestly - they need the UEFI flavor. */
        ocbl_serial_puts("[boot] chainload\r\n");
        ocbl_wdt_disarm();
        ocbl_chain_vbr(e->drive, e->base_lba, e->ssz);
        for (;;)
            __asm__ volatile("cli; hlt");
    }


    ocbl_volume_t v;
    if (!ocbl_fs_mount(e->drive, e->base_lba, e->ssz, &v)) {
        ocbl_serial_puts("[boot] FATAL: payload volume remount failed\r\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    }

    const char* slot = 0;
    if (e->type == OCBL_ENT_OCOS)
        slot = e->slot == 1 ? "A" : (e->slot == 2 ? "B" : "ISO");

    /* the mb2 "self" module: a raw copy of the kernel ELF at 0x800000
     * (install/abdisk copy it onto target disks from there) */
    u32 self_size = 0;
    if (e->type == OCBL_ENT_OCOS)
        self_size = ocbl_load_raw(&v, e->path, OCBL_SELF_BASE);

    ocbl_fb_info_t fb;
    bool fb_ok = ocbl_vbe_set_800x600x32(&fb);
    ocbl_serial_puts(fb_ok ? "[boot] VBE 800x600x32 ok\r\n"
                           : "[boot] VBE FAILED (kernel will refuse)\r\n");

    u64 base = e->type == OCBL_ENT_OCIRE ? OCBL_OCIRE_BASE : 0x100000ull;
    u32 ksize = 0;
    u64 entry = ocbl_load_elf(&v, e->path, base, &ksize);
    if (!entry) {
        ocbl_serial_puts("[boot] FATAL: kernel load failed\r\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    }
    ocbl_serial_puts("[boot] kernel loaded: entry=0x");
    ocbl_serial_hex(entry, 8);
    ocbl_serial_puts(" size=");
    ocbl_serial_dec(ksize);
    ocbl_serial_puts("\r\n");

    u64 self_start = self_size ? OCBL_SELF_BASE : 0;
    u64 self_end = self_start + self_size;
    u32 mbisz = ocbl_build_mbi(slot, self_start, self_end,
                               fb_ok ? &fb : 0);
    ocbl_serial_puts("[boot] mbi built: ");
    ocbl_serial_dec(mbisz);
    ocbl_serial_puts(" bytes at 0x50000\r\n");
    /* fresh 60 s window for the payload to take the watchdog over */
    ocbl_wdt_arm(60);
    ocbl_handoff(entry, OCBL_MBI_BASE);
}
