/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocbl/efi/ocbl_raw.c
 * Purpose: the RAW UEFI-flavor entry point.  Since
 *          WP-runtime-hardening the UEFI track is: OVMF ->
 *          OCRMECL (BOOTX64.EFI, performs every firmware call,
 *          ExitBootServices, GDT/IDT rewrite, NX/RO strip) ->
 *          THIS entry, with the handoff blob in RCX (MS x64 ABI,
 *          interface/boot_handoff.h).
 *
 *          From here the flow is the standard OCBL boot minus every
 *          firmware dependency: the RAM ISO is the only disk
 *          (ocbl_ramdisk.c), the framebuffer console renders the
 *          menu (ocbl_con_efi.c + ocbl_font_data.c), the captured
 *          EBS memory map feeds the mb2 tag (ocbl_mmap_efi.c), and
 *          the payload handoff goes through the same 0x30000
 *          downgrade trampoline as before (ocbl_efi_handoff_blob.asm).
 *
 *          The legacy firmware-flow files (efi_entry.c and its
 *          ConOut/BlockIo/GOP/RSDP/chainload backends) are GONE -
 *          their responsibilities moved into OCRMECL; what could
 *          not move (chainload of foreign loaders) is honestly
 *          parked: post-EBS there is no LoadImage.
 */
#include "ocbl_efi_def.h"
#include "../common/ocbl_serial.h"
#include "../common/ocbl_scan.h"
#include "../common/ocbl_disk.h"
#include "../common/ocbl_menu.h"
#include "../common/ocbl_boot.h"
#include "../common/ocbl_wdt.h"
#include "../common/ocbl_fs.h"
#include "../common/ocbl_fb.h"
#include "../common/ocbl_mmap.h"
#include "../common/ocbl_statemon.h"
#include "ocbl_state.h"
#include "boot_handoff.h"

/* efi/ocbl_efi_handoff_data.asm (incbin of handoff_blob.bin) */
extern char ocbl_efi_handoff_blob[];
extern char ocbl_efi_handoff_blob_end[];

/* the PE converter leaves .bss as raw-vs-virtual slack and the
 * EDK2 loader does NOT zero-fill it (the efi_entry.c lesson) */
extern char __bss_start[];
extern char __bss_end[];

/* efi/ocbl_ramdisk.c, efi/ocbl_con_efi.c, efi/ocbl_mmap_efi.c */
void ocbl_ramdisk_init(const ocr_uefi_handoff_t* h);
void ocbl_fbcon_init(const ocr_uefi_handoff_t* h);
u32 ocbl_mmap_from_captured(const ocr_uefi_handoff_t* h);

#ifndef OCBL_RELEASE
#define OCBL_RELEASE "3.0.0"
#endif
#ifndef OCBL_BUILD
#define OCBL_BUILD 0
#endif

static const ocr_uefi_handoff_t* g_handoff;

/* the RSDP OCRMECL captured from the EFI configuration table */
u64 ocbl_rsdp_find(void)
{
    return g_handoff ? g_handoff->rsdp : 0;
}

static void park(const char* why)
{
    ocbl_serial_puts("[boot] ");
    ocbl_serial_puts(why);
    ocbl_serial_puts(" - halting\r\n");
    for (;;)
        __asm__ volatile("hlt");
}

/* ---- the raw entry (MS x64 ABI: RCX = handoff, RDX = magic) ---- */
EFI_CALL void ocbl_raw_entry(const ocr_uefi_handoff_t* h, u64 magic)
{
    if (magic != OCR_UEFI_HANDOFF_MAGIC || !h) {
        /* ABI violation - the serial marker is all we can honestly do */
        __asm__ volatile(
            "movw $0x3F8, %%dx\n\t"
            "movb $0x21, %%al\n\t"     /* '!' */
            "outb %%al, %%dx\n\t"
            "cli\n1: hlt\n\tjmp 1b\n"
            : : : "ax", "dx");
    }

    for (char* p = __bss_start; p < __bss_end; p++)
        *p = 0;
    g_handoff = h;

    ocbl_serial_init();
    ocbl_serial_puts("\r\nOCBL 3.0.0 boot loader (" OCBL_RELEASE " build ");
    ocbl_serial_dec(OCBL_BUILD);
    ocbl_serial_puts(")\r\n");
    ocbl_serial_puts("RAW flavor entry OK (post-EBS, via OCRMECL)\r\n");

    /* the platform console + the RAM disk */
    ocbl_fbcon_init(h);
    ocbl_ramdisk_init(h);

    /* the boot-state monitor: OCRMECL reserved the window before EBS,
     * so it is available by construction */
    ocbl_statemon_boot_check(1);

    /* the scanner on the RAM ISO */
    u32 entries = ocbl_scan_all();
    ocbl_scan_dump();
    ocbl_serial_puts("[raw] scan complete: ");
    ocbl_serial_dec(entries);
    ocbl_serial_puts(" entries\r\n");

    ocbl_wdt_probe();
    if (ocbl_wdt_present())
        ocbl_wdt_arm(60);

    /* a pending unrouted BOOT_FAILED record auto-boots the OCIRE
     * entry (the WP07a one-shot) */
    int auto_route = ocbl_statemon_route_pending();
    u32 pick = (u32)-1;
    if (auto_route) {
        ocbl_scan_t* rt = ocbl_scan_result();
        for (u32 i = 0; i < rt->count; i++) {
            if (rt->entries[i].type == OCBL_ENT_OCIRE) {
                pick = i;
                break;
            }
        }
        if (pick == (u32)-1)
            ocbl_serial_puts("[state] no OCIRE entry found by the scan - "
                             "falling back to the menu\r\n");
    }
    if (pick == (u32)-1)
        pick = ocbl_menu_run();
    ocbl_menu_announce(pick);

    ocbl_scan_t* t = ocbl_scan_result();
    if (pick >= t->count) {
        ocbl_serial_puts("[boot] ACPI S5 shutdown\r\n");
        ocbl_wdt_disarm();
        ocbl_acpi_enter_s5();
        park("S5 did not power off");
    }
    ocbl_entry_t* e = &t->entries[pick];
    if (e->type != OCBL_ENT_OCOS && e->type != OCBL_ENT_OCIRE &&
        e->type != OCBL_ENT_OCSE) {
        /* chainload of a foreign loader needs boot services
         * (LoadImage/StartImage) - post-EBS that is impossible by
         * design; park honestly instead of guessing */
        park("EFI chainload is unavailable in the raw flavor "
             "(boot the volume through the firmware boot menu)");
    }

    if (!auto_route && e->type == OCBL_ENT_OCIRE)
        ocbl_statemon_user_request();

    /* ---- the mb2 payload path ---- */
    ocbl_volume_t v;
    if (!ocbl_fs_mount(e->drive, e->base_lba, e->ssz, &v))
        park("FATAL: payload volume remount failed");

    const char* slot = 0;
    if (e->type == OCBL_ENT_OCOS)
        slot = e->slot == 1 ? "A" : (e->slot == 2 ? "B" : "ISO");

    if (e->type == OCBL_ENT_OCOS) {
        ocbl_statemon_probe_hangat(&v);
        ocbl_statemon_probe_tickpit(&v);   /* B4: oc.tick=pit hook */
        ocbl_statemon_probe_nosplash(&v); /* B5: oc.splash=0 hook */
    }

    u32 self_size = 0;
    if (e->type == OCBL_ENT_OCOS)
        self_size = ocbl_load_raw(&v, e->path, OCBL_SELF_BASE);

    ocbl_fb_info_t fb;
    bool fb_ok = false;
    if (g_handoff->fb_addr) {
        fb.addr = g_handoff->fb_addr;
        fb.pitch = g_handoff->fb_pitch;
        fb.width = g_handoff->fb_width;
        fb.height = g_handoff->fb_height;
        fb.bpp = (u8)g_handoff->fb_bpp;
        if (g_handoff->fb_rgb) {           /* RGBx: R@16 G@8 B@0 */
            fb.red_pos = 16;  fb.red_size = 8;
            fb.green_pos = 8; fb.green_size = 8;
            fb.blue_pos = 0;  fb.blue_size = 8;
        } else {                            /* BGRx: R@0 G@8 B@16 */
            fb.red_pos = 0;   fb.red_size = 8;
            fb.green_pos = 8; fb.green_size = 8;
            fb.blue_pos = 16; fb.blue_size = 8;
        }
        fb_ok = true;
    }
    if (!fb_ok)
        ocbl_serial_puts("[boot] no framebuffer (kernel will refuse)\r\n");

    u64 base = e->type == OCBL_ENT_OCIRE ? OCBL_OCIRE_BASE : 0x100000ull;
    u32 ksize = 0;
    u64 entry = ocbl_load_elf(&v, e->path, base, &ksize);
    if (!entry)
        park("FATAL: kernel load failed");
    ocbl_serial_puts("[boot] kernel loaded: entry=0x");
    ocbl_serial_hex(entry, 8);
    ocbl_serial_puts(" size=");
    ocbl_serial_dec(ksize);
    ocbl_serial_puts("\r\n");

    u64 self_start = self_size ? OCBL_SELF_BASE : 0;
    u64 self_end = self_start + self_size;

    if (e->type == OCBL_ENT_OCOS) {
        ocbl_statemon_arm_payload();
    } else {
        ocbl_wdt_disarm();
    }

    /* the OCRMECL-captured EBS map -> the shared E820 buffer */
    u32 nmap = ocbl_mmap_from_captured(h);
    if (!nmap)
        park("FATAL: memory map conversion failed");
    u32 mbisz = ocbl_build_mbi(slot, self_start, self_end,
                               fb_ok ? &fb : 0);
    ocbl_serial_puts("[boot] mbi built: ");
    ocbl_serial_dec(mbisz);
    ocbl_serial_puts(" bytes at 0x50000 (");
    ocbl_serial_dec(nmap);
    ocbl_serial_puts(" mmap ranges)\r\n");

    /* copy the downgrade trampoline into the reserved bounce window
     * and drop into it (0x30000 - reserved by OCRMECL before EBS) */
    {
        const u8* b0 = (const u8*)ocbl_efi_handoff_blob;
        const u8* b1 = (const u8*)ocbl_efi_handoff_blob_end;
        u8* dst = (u8*)(u64)OCBL_BOUNCE;
        u64 n = (u64)(b1 - b0);
        /* LIVE-CAUGHT (first UEFI full boot): -O2 folded the copy into
         * delta addressing (rcx = src - dst as ONE folded immediate).
         * The delta is negative at link time (the blob's link VMA is
         * below 0x30000) and the PE converter emits no DIR64 for it,
         * so the runtime loop read the LINK-time source - a zero page -
         * and the blob landed as 156 zero bytes.  The asm barrier makes
         * both pointers opaque; the loop then addresses each pointer
         * with its own (relocated) movabs, exactly like every other
         * absolute reference in this image. */
        __asm__ volatile("" : "+r"(b0), "+r"(dst));
        for (u64 i = 0; i < n; i++)
            dst[i] = b0[i];
        ocbl_serial_puts("[boot] handoff trampoline at 0x30000 (");
        ocbl_serial_dec((u32)n);
        ocbl_serial_puts(" bytes)\r\n");
        ((void (*)(u64, u64))(u64)OCBL_BOUNCE)(entry, OCBL_MBI_BASE);
    }

    park("FATAL: the handoff trampoline returned");
}
