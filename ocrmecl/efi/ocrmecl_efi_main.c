/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocrmecl/efi/ocrmecl_efi_main.c
 * Purpose: the OCRMECL UEFI track - the boot compatibility layer that
 *          runs as \EFI\BOOT\BOOTX64.EFI, BEFORE OCBL, and performs
 *          every firmware service call of the boot:
 *
 *            1. connect sweep + BlockIo enumeration (the OVMF BDS
 *               only connects the boot chain - the WP-05 B5-3 lesson)
 *            2. find the boot ISO (valid PVD + /OCBL.EFI present) and
 *               read the WHOLE medium into a RAM buffer
 *            3. locate \OCBL.EFI in the RAM copy, LoadImage from
 *               buffer (the firmware applies .reloc), parse the PE
 *               entry RVA - never StartImage
 *            4. set the GOP mode (800x600x32 preferred) and capture
 *               the framebuffer descriptor
 *            5. capture the RSDP from the EFI configuration table
 *            6. reserve the shared fixed low-memory windows
 *               (0x30000/0x50000/0x60000/0x98000 - the layout every
 *               OCBL common layer addresses by absolute address)
 *            7. disarm the firmware watchdog, GetMemoryMap,
 *               ExitBootServices (the retry discipline)
 *            8. rewrite GDTR/IDTR with own tables (64-bit code/data,
 *               256 gates -> a marked hlt stub), reload all segments
 *            9. walk CR3 and strip NX (bit 63) + force RW (bit 1) on
 *               every present entry at all four levels - the firmware
 *               page tables stay in force, minus the restrictions
 *           10. jump to OCBL's raw entry (MS x64 ABI: RCX = handoff
 *               struct, RDX = magic; interface/boot_handoff.h)
 *
 *          After step 7 no firmware service is ever called again -
 *          OCBL runs firmware-free on the flattened machine.
 */
#include "ocrmecl_efi_def.h"
#include "boot_handoff.h"

#ifndef OCBL_RELEASE
#define OCBL_RELEASE "3.0.0"
#endif
#ifndef OCBL_BUILD
#define OCBL_BUILD 0
#endif

/* ---- GUIDs (UEFI spec appendix A) ---- */
static const efi_guid k_block_io_guid = {
    0x964e5b21, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const efi_guid k_gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };
static const efi_guid k_loaded_image_guid = {
    0x5b1b31a1, 0x9562, 0x11d2,
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const efi_guid k_acpi20_guid = {
    0x8868e871, 0xe4f1, 0x11d3,
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
static const efi_guid k_acpi10_guid = {
    0xeb9d2d30, 0x2d88, 0x11d3,
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };

/* ---- globals ---- */
static efi_handle g_image;
static efi_system_table* g_st;
static efi_boot_services* g_bs;

/* the handoff blob lives in .bss of THIS image - the image pages stay
 * allocated post-EBS (Loader*), so the pointer stays valid until the
 * kernel takes over */
static ocr_uefi_handoff_t g_handoff;

/* the ExitBootServices memory map capture (descriptors are desc_size
 * apart; 640 x 48 B = 30 KiB covers the busiest OVMF fleets) */
#define MMAP_MAX 640
static efi_memory_descriptor g_mmap[MMAP_MAX];
static u64 g_mmap_bytes;
static u64 g_mmap_desc_size;
static u32 g_mmap_version;

/* own GDT/IDT (loaded post-EBS) */
static u64 g_gdt[3];                       /* null + code64 + data64 */
static u64 g_idt[256 * 2];                 /* 256 sixteen-byte gates */

extern char __bss_start[];
extern char __bss_end[];

/* ---- helpers ---- */
static void fatal(const char* why)
{
    ocr_serial_puts("OCRMECL: FATAL ");
    ocr_serial_puts(why);
    ocr_serial_puts("\r\n");
    for (;;)
        __asm__ volatile("hlt");
}

static int guid_eq(const efi_guid* a, const efi_guid* b)
{
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    for (int i = 0; i < 16; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

/* ---- GOP mode accessors (the OVMF +4 head quirk, live-caught in
 *      WP-05 B5-5 and honored exactly as ocbl_gop_efi.c does) ---- */
static u32 gmi_h(const efi_gop_mode_info* mi)  { return ((const u32*)mi)[1]; }
static u32 gmi_v(const efi_gop_mode_info* mi)  { return ((const u32*)mi)[2]; }
static u32 gmi_fmt(const efi_gop_mode_info* mi){ return ((const u32*)mi)[3]; }
static u32 gmi_ppsl(const efi_gop_mode_info* mi){ return ((const u32*)mi)[8]; }

/* ---- the 256-gate IDT fault stub (64-bit): '!' on COM1 then a
 *      marked halt - never returns ---------------------------------- */
static void fault_stub(void)
{
    /* the diagnostic stub: '!' then the interrupt-frame RIP and the
     * faulting linear address (CR2) - enough to place any post-EBS
     * exception on the map (hex nibbles on the same serial line). */
    __asm__ volatile(
        "movw $0x3F8, %%dx\n\t"
        "movb $0x21, %%al\n\t"          /* '!' */
        "outb %%al, %%dx\n\t"
        "mov %%rsp, %%rsi\n\t"          /* interrupt frame: [rsp]=RIP */
        "movq (%%rsi), %%rax\n\t"
        "call 1f\n\t"                   /* rax = faulting RIP */
        "mov %%cr2, %%rax\n\t"
        "call 1f\n\t"                   /* rax = fault address */
        "cli\n"
        "2: hlt\n\t"
        "jmp 2b\n"
        "1: mov %%dx, %%dx\n\t"
        "mov $8, %%cx\n"
        "3: rol $4, %%rax\n\t"
        "mov %%al, %%bl\n\t"
        "and $15, %%bl\n\t"
        "cmp $10, %%bl\n\t"
        "jb 4f\n\t"
        "add $7, %%bl\n"
        "4: add $48, %%bl\n\t"
        "mov %%bl, %%al\n\t"
        "outb %%al, %%dx\n\t"
        "loop 3b\n\t"
        "ret\n"
        : : : "ax", "bx", "cx", "dx", "si");
}

/* ---- post-EBS table rewrite ---------------------------------------- */
static void load_own_tables(void)
{
    g_gdt[0] = 0;
    g_gdt[1] = 0x00209A0000000000ULL;      /* 0x08 code64 (L=1)       */
    g_gdt[2] = 0x0000920000000000ULL;      /* 0x10 data64             */

    struct { u16 limit; u64 base; } __attribute__((packed)) gdtr;
    gdtr.limit = sizeof(g_gdt) - 1;
    gdtr.base = (u64)g_gdt;

    /* the fault stub address split across the 16-byte gate format */
    u64 stub = (u64)&fault_stub;
    for (int v = 0; v < 256; v++) {
        u64* g = &g_idt[v * 2];
        g[0] = (stub & 0xFFFFULL) | (0x0008ULL << 16) |
               (0x8EULL << 40) | ((stub >> 16) & 0xFFFFULL) << 48;
        g[1] = stub >> 32;
    }
    struct { u16 limit; u64 base; } __attribute__((packed)) idtr;
    idtr.limit = sizeof(g_idt) - 1;
    idtr.base = (u64)g_idt;

    __asm__ volatile("lgdt %0" : : "m"(gdtr));
    __asm__ volatile(
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:"
        : : : "ax");
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

/* ---- the NX/RO page-table strip ------------------------------------
 * Walks the live CR3 tree and, on every PRESENT entry at all four
 * levels, clears NX (bit 63) and sets RW (bit 1) - permission
 * RELAXATION only, so in-flight TLB entries can never fault.  The
 * node budget bounds hostile/looping tables (real firmware trees are
 * a few dozen tables). */
static u64 g_pt_budget;
static void strip_level(u64* table, int level)
{
    if (g_pt_budget-- == 0)
        return;
    for (int i = 0; i < 512; i++) {
        u64 e = table[i];
        if (!(e & 1))
            continue;
        u64 phys = e & 0x000FFFFFFFFFF000ULL;
        int huge = (level >= 1 && level <= 2 && (e & (1ULL << 7)));
        e &= ~(1ULL << 63);
        e |= 2;
        table[i] = e;
        if (level > 0 && !huge)
            strip_level((u64*)phys, level - 1);
    }
}

static void strip_page_tables(void)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    /* LIVE-CAUGHT (first UEFI boot): the firmware maps its own page
     * tables READ-ONLY - the very first entry write faulted straight
     * into the 256-gate stub ('!').  CR0.WP=0 lets supervisor writes
     * through the RO mappings for the duration of the strip; the walk
     * then sets RW on every entry, and WP goes back on afterwards. */
    u64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 & ~(1ULL << 16)));
    g_pt_budget = 4096;
    strip_level((u64*)(cr3 & 0x000FFFFFFFFFF000ULL), 3);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

/* ---------------------------------------------------------------------
 * The entry point (MS x64 ABI)
 * ------------------------------------------------------------------- */
EFI_CALL efi_status ocr_efi_main(efi_handle image, efi_system_table* st)
{
    g_image = image;
    g_st = st;
    g_bs = st->boot_services;

    /* the PE converter leaves .bss as raw-vs-virtual slack and EDK2's
     * loader does NOT zero-fill it (the ocbl efi_entry lesson) */
    for (char* p = __bss_start; p < __bss_end; p++)
        *p = 0;

    ocr_serial_init();
    ocr_serial_puts("\r\nOCRMECL 3.0.0 boot compatibility layer (");
    ocr_serial_puts(OCBL_RELEASE " build ");
    ocr_serial_dec(OCBL_BUILD);
    ocr_serial_puts(")\r\nOCRMECL: UEFI track entry OK\r\n");

    /* ---- 1. the connect sweep (the OVMF BDS only connects the boot
     *          chain; a whole-disk scan needs the recursive sweep) */
    {
        u64 nh = 0;
        efi_handle* hb = 0;
        if (g_bs->locate_handle_buffer(0, 0, 0, &nh, &hb) == EFI_SUCCESS
            && hb && nh) {
            u64 connected = 0;
            for (u64 i = 0; i < nh; i++)
                if (g_bs->connect_controller(hb[i], 0, 0, 1) == EFI_SUCCESS)
                    connected++;
            g_bs->free_pool(hb);
            ocr_serial_puts("OCRMECL: connect sweep ");
            ocr_serial_dec(connected);
            ocr_serial_puts(" controllers\r\n");
        }
    }

    /* ---- 2. find the boot ISO among the whole-disk BlockIo units,
     *          read the WHOLE medium into RAM ---- */
    u64 nr = 0;
    efi_handle* buf = 0;
    if (g_bs->locate_handle_buffer(2, &k_block_io_guid,
                                   (void*)&k_block_io_guid,
                                   &nr, &buf) != EFI_SUCCESS || !buf)
        fatal("no BlockIo handles");
    efi_block_io* iso_io = 0;
    u64 iso_bytes = 0;
    u8 pvd_sec[2048];
    for (u64 i = 0; i < nr; i++) {
        efi_block_io* io = 0;
        if (g_bs->handle_protocol(buf[i], &k_block_io_guid,
                                  (void**)&io) != EFI_SUCCESS ||
            !io || !io->media)
            continue;
        efi_status rs = io->read_blocks(io, io->media->media_id, 16,
                                        2048, pvd_sec);
        int pvdok = (rs == EFI_SUCCESS) && ocr_iso_pvd_sector(pvd_sec);
        ocr_serial_puts("OCRMECL: bio[");
        ocr_serial_dec(i);
        ocr_serial_puts("] bs=");
        ocr_serial_dec(io->media->block_size);
        ocr_serial_puts(" log=");
        ocr_serial_dec(io->media->logical_partition);
        ocr_serial_puts(" media=");
        ocr_serial_dec(io->media->media_present);
        ocr_serial_puts(" rd=");
        ocr_serial_dec(rs == EFI_SUCCESS);
        ocr_serial_puts(" pvd=");
        ocr_serial_dec(pvdok);
        ocr_serial_puts(" bytes=");
        for (int bi = 0; bi < 12; bi++) {
            u8 b = (rs == EFI_SUCCESS) ? pvd_sec[bi] : 0;
            char t[2];
            t[0] = "0123456789ABCDEF"[b >> 4];
            t[1] = 0;
            ocr_serial_puts(t);
            t[0] = "0123456789ABCDEF"[b & 15];
            ocr_serial_puts(t);
        }
        ocr_serial_puts("\r\n");
        if (io->media->logical_partition || !io->media->media_present ||
            io->media->block_size != 2048)
            continue;
        if (!pvdok)
            continue;
        iso_io = io;
        iso_bytes = (io->media->last_block + 1) * 2048;
        break;
    }
    g_bs->free_pool(buf);
    if (!iso_io)
        fatal("no ISO9660 boot medium found");

    u64 pages = (iso_bytes + 0xFFF) >> 12;
    efi_phys iso_phys = 0;
    /* BOOT_SERVICES_DATA: post-EBS it becomes E820-usable, which is
     * exactly right - the kernel owns (and may reuse) the ramdisk
     * pages once OCBL has copied everything it needs out of them */
    if (g_bs->allocate_pages(0 /* ANY */, 4 /* BOOT_SERVICES_DATA */,
                             pages, &iso_phys) != EFI_SUCCESS)
        fatal("ram ISO allocation failed");
    if (iso_io->read_blocks(iso_io, iso_io->media->media_id, 0,
                            iso_bytes, (void*)iso_phys) != EFI_SUCCESS)
        fatal("whole-medium read failed");
    ocr_serial_puts("OCRMECL: boot ISO in RAM: ");
    ocr_serial_dec(iso_bytes);
    ocr_serial_puts(" bytes at ");
    ocr_serial_hex(iso_phys, 12);
    ocr_serial_puts("\r\n");

    /* ---- 3. locate \OCBL.EFI, LoadImage from buffer ---- */
    u64 ocbl_lba = 0, ocbl_sz = 0;
    if (!ocr_iso_find((const u8*)iso_phys, iso_bytes, "OCBL.EFI",
                      &ocbl_lba, &ocbl_sz))
        fatal("OCBL.EFI not found on the ISO");
    efi_handle ocbl_image = 0;
    if (g_bs->load_image(0 /* boot policy */, g_image, 0,
                         (void*)(iso_phys + ocbl_lba * 2048), ocbl_sz,
                         &ocbl_image) != EFI_SUCCESS)
        fatal("LoadImage(OCBL.EFI) failed");
    efi_loaded_image* li = 0;
    if (g_bs->handle_protocol(ocbl_image, &k_loaded_image_guid,
                              (void**)&li) != EFI_SUCCESS || !li)
        fatal("LoadedImage protocol failed");
    /* parse the PE header for AddressOfEntryPoint (PE32+) */
    const u8* pe = (const u8*)li->image_base;
    u32 lfanew = *(const u32*)(pe + 0x3C);
    u16 opt_magic = *(const u16*)(pe + lfanew + 24);
    if (opt_magic != 0x20B)
        fatal("OCBL.EFI is not PE32+");
    u32 entry_rva = *(const u32*)(pe + lfanew + 24 + 16);
    ocr_serial_puts("OCRMECL: OCBL loaded at ");
    ocr_serial_hex((u64)li->image_base, 12);
    ocr_serial_puts(" entry ");
    ocr_serial_hex((u64)li->image_base + entry_rva, 12);
    ocr_serial_puts("\r\n");

    /* ---- 4. GOP: prefer 800x600x32 (the mode the BIOS flavor picks
     *          through VBE - one kernel expectation, two tracks) ---- */
    u64 fb_addr = 0; u32 fb_pitch = 0, fb_w = 0, fb_h = 0, fb_bpp = 0;
    u8 fb_bgr = 0;
    {
        efi_graphics_output* gop = 0;
        if (g_bs->locate_protocol(&k_gop_guid, 0, (void**)&gop)
            == EFI_SUCCESS && gop && gop->mode) {
            u32 chosen = gop->mode->mode;
            for (u32 i = 0; i < gop->mode->max_mode; i++) {
                u64 isz = 0;
                efi_gop_mode_info* mi = 0;
                if (gop->query_mode(gop, i, &isz, &mi) != EFI_SUCCESS || !mi)
                    continue;
                if (gmi_h(mi) == 800 && gmi_v(mi) == 600 && gmi_fmt(mi) <= 1) {
                    chosen = i;
                    break;
                }
            }
            if (chosen != gop->mode->mode)
                gop->set_mode(gop, chosen);
            efi_gop_mode_info* mi = gop->mode->info;
            if (mi && gop->mode->frame_buffer_base) {
                u32 fmt = gmi_fmt(mi);
                if (fmt <= 1) {
                    fb_addr = gop->mode->frame_buffer_base;
                    fb_w = gmi_h(mi);
                    fb_h = gmi_v(mi);
                    fb_pitch = gmi_ppsl(mi) * 4;
                    fb_bpp = 32;
                    fb_bgr = (fmt == 1);
                }
            }
        }
    }
    if (!fb_addr)
        ocr_serial_puts("OCRMECL: GOP: no linear fb (kernel headless)\r\n");
    else {
        ocr_serial_puts("OCRMECL: GOP ");
        ocr_serial_dec(fb_w);
        ocr_serial_puts("x");
        ocr_serial_dec(fb_h);
        ocr_serial_puts("x32 fb ");
        ocr_serial_hex(fb_addr, 12);
        ocr_serial_puts("\r\n");
    }

    /* ---- 5. RSDP from the configuration table ---- */
    u64 rsdp = 0;
    {
        const efi_config_table* t =
            (const efi_config_table*)g_st->config_table;
        for (u64 i = 0; i < g_st->nr_table_entries && t; i++) {
            if (guid_eq(&t[i].vendor_guid, &k_acpi20_guid)) {
                rsdp = (u64)t[i].vendor_table;
                break;
            }
            if (guid_eq(&t[i].vendor_guid, &k_acpi10_guid) && !rsdp)
                rsdp = (u64)t[i].vendor_table;
        }
    }

    /* ---- 6. reserve the shared fixed low-memory windows ---- */
    {
        struct { u64 addr, bytes; } w[] = {
            { 0x30000, 0x10000 },   /* loader bounce window          */
            { 0x50000, 0x10000 },   /* multiboot2 info (64 KiB)      */
            { 0x60000, 0x23000 },   /* mmap + secbuf + FAT cache     */
            { 0x98000, 0x4000 },    /* boot-state monitor window     */
        };
        for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
            efi_phys a = w[i].addr;
            if (g_bs->allocate_pages(2 /* ADDRESS */, 4, 
                                     (w[i].bytes + 0xFFF) >> 12,
                                     &a) != EFI_SUCCESS)
                fatal("fixed low-memory window not reservable");
        }
        ocr_serial_puts("OCRMECL: fixed windows reserved "
                        "(0x30000/0x50000/0x60000/0x98000)\r\n");
    }

    /* ---- 7. watchdog off, GetMemoryMap, ExitBootServices ---- */
    g_bs->set_watchdog_timer(0, 0, 0, 0);
    int exited = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        u64 sz = sizeof(g_mmap);
        u64 key = 0, dsz = 0;
        u32 ver = 0;
        efi_status s = g_bs->get_memory_map(&sz, g_mmap, &key, &dsz, &ver);
        if (s != EFI_SUCCESS)
            fatal("GetMemoryMap failed");
        g_mmap_bytes = sz;
        g_mmap_desc_size = dsz;
        g_mmap_version = ver;
        if (g_bs->exit_boot_services(g_image, key) == EFI_SUCCESS) {
            exited = 1;
            break;
        }
        ocr_serial_puts("OCRMECL: EBS retry - map key expired\r\n");
    }
    if (!exited)
        fatal("ExitBootServices refused");
    ocr_serial_puts("OCRMECL: boot services exited\r\n");

    /* ---- 8. own GDT/IDT, then the NX/RO strip (no firmware service
     *          from here on - only instructions and memory) ---- */
    load_own_tables();
    ocr_serial_puts("OCRMECL: gdt+idt(256 gates) loaded\r\n");
    strip_page_tables();
    ocr_serial_puts("OCRMECL: page tables NX/RO stripped\r\n");

    /* ---- 9. fill the handoff and jump (the raw-entry ABI) ---- */
    g_handoff.magic = OCR_UEFI_HANDOFF_MAGIC;
    g_handoff.version = 1;
    g_handoff.iso_base = iso_phys;
    g_handoff.iso_size = iso_bytes;
    g_handoff.ocbl_base = (u64)li->image_base;
    g_handoff.ocbl_size = li->image_size;
    g_handoff.ocbl_entry = (u64)li->image_base + entry_rva;
    g_handoff.fb_addr = fb_addr;
    g_handoff.fb_pitch = fb_pitch;
    g_handoff.fb_width = fb_w;
    g_handoff.fb_height = fb_h;
    g_handoff.fb_bpp = fb_bpp;
    g_handoff.fb_rgb = (u8)!fb_bgr;
    g_handoff.rsdp = rsdp;
    g_handoff.mmap = (u64)g_mmap;
    g_handoff.mmap_size = g_mmap_bytes;
    g_handoff.mmap_desc_size = g_mmap_desc_size;
    g_handoff.mmap_desc_version = g_mmap_version;
    g_handoff.serial_ok = 1;

    ocr_serial_puts("OCRMECL: machine flattened - handing off to OCBL\r\n");
    ((ocbl_raw_entry_fn)g_handoff.ocbl_entry)(&g_handoff,
                                              OCR_UEFI_HANDOFF_MAGIC);

    /* the raw entry never returns; if it does, park honestly */
    fatal("OCBL raw entry returned");
    return EFI_LOAD_ERROR;
}
