/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: interface/boot_handoff.h
 * Purpose: the physical machine-state handoff contract between the
 *          OCRMECL boot compatibility layer and the OCBL boot loader.
 *
 *          OCRMECL (Open Cube Runtime Machine Environment Compatibility
 *          Layer) is a PHYSICALLY SEPARATE boot stage that runs before
 *          OCBL on both firmware tracks and flattens the machine state
 *          every QEMU/SeaBIOS/OVMF/VMware firmware leaves behind in a
 *          different shape.  This header is the ONLY shared contract
 *          between the two trees - OCRMECL never includes an OCBL
 *          header and OCBL never includes an OCRMECL header.
 *
 * ---------------------------------------------------------------------
 * BIOS track (real-mode handoff - the classic El Torito contract)
 * ---------------------------------------------------------------------
 * OCRMECL.BIN is the ISO El Torito no-emulation image: SeaBIOS preloads
 * [ocr-stage1 512B][ocr-core][OCBL.BIN] at linear 0x7C00.  OCRMECL
 * flattens the machine, then MOVES the embedded OCBL.BIN down to
 * 0x7C00 (backward copy from a stub at 0x6000 - the only live code
 * below the destination) and far-jumps into it exactly as the firmware
 * would have:
 *
 *   CS:IP = 0000:0x7C00      DL = boot drive (passed through unchanged)
 *   IF = 0 (cli)             DF = 0
 *   SS:SP = 0000:0x7C00      (OCBL stage1 re-arms its own stack)
 *   whole OCBL.BIN image at [0x7C00, 0x7C00+size)
 *   A20 = forced ON (verified by the double-pattern alias test)
 *   GDTR = the OCRMECL temp GDT at linear 0x1000 (5 flat entries)
 *   IDTR = the OCRMECL 256-gate IDT at linear 0x2000 - every gate
 *          vectors to a serial-marked hlt stub at 0x1100.  This closes
 *          the historical "inherited firmware IVT" window: any vector
 *          between OCBL stage1's PE switch and ocbl_idt_setup() now
 *          halts with a serial '!' marker instead of triple-faulting
 *          through a real-mode-sized table.
 *   8259A = remapped to vectors 0x08..0x0F / 0x70..0x77, all masked
 *   IOAPIC = all redirection-table entries masked (unreal-mode MMIO)
 *   PIC/PS2/DMA leftovers = EOI'd, drained, masked (the flatten set)
 *   tables at 0x1000/0x1100/0x2000 SURVIVE the OCBL move (they live
 *          below 0x7C00) - the IDT stays valid until OCBL replaces it.
 *
 * OCBL's BIOS stage1 needs ZERO changes: it re-writes the 0x520
 * transfer block, re-checks A20, loads its own GDT and enters PE -
 * every value above is either what the firmware already provided or
 * strictly tighter.
 *
 * ---------------------------------------------------------------------
 * UEFI track (post-ExitBootServices raw entry - MS x64 ABI)
 * ---------------------------------------------------------------------
 * OCRMECL is \EFI\BOOT\BOOTX64.EFI; OCBL's PE is a plain ISO9660 file
 * (\OCBL.EFI).  OCRMECL performs every firmware service call of the
 * boot (BlockIo ISO read, LoadImage-from-buffer relocation, GOP mode
 * set, GetMemoryMap, watchdog disarm, ExitBootServices) and then
 * flattens the post-firmware machine:
 *
 *   1. load the WHOLE boot ISO into a RAM buffer (AllocatePages)
 *   2. locate \OCBL.EFI in that RAM copy (ISO9660 walk), LoadImage
 *      from-buffer (the firmware applies .reloc), parse the PE header
 *      for the entry RVA - never StartImage
 *   3. set the GOP mode, capture the framebuffer descriptor
 *   4. reserve the shared fixed low-memory windows (0x30000/0x50000/
 *      0x60000/0x98000 - identical to the pre-OCRMECL OCBL layout)
 *   5. set_watchdog_timer(0), GetMemoryMap
 *   6. ExitBootServices (the query->EBS retry discipline)
 *   7. rewrite GDTR/IDTR with OCRMECL's own tables (in-image, 256
 *      gates), reload all data segments
 *   8. walk CR3 and strip NX (bit 63) + force RW (bit 1) on every
 *      present PML4/PDPT/PD/PT entry - the firmware page tables stay
 *      in force, minus the RO/NX restrictions
 *   9. jump to OCBL's raw entry:
 *
 *        RCX = ocr_uefi_handoff_t* (this struct, below)
 *        RDX = OCR_UEFI_HANDOFF_MAGIC
 *        stack: the firmware stack, aligned, plenty of headroom
 *
 * OCBL's raw entry (ocbl_raw_entry, the new UEFI-flavor main) then
 * runs the standard flow - scan/menu/load/MBI/kernel handoff - with
 * the RAM ISO as its only disk backend.  No firmware service is ever
 * called after the handoff.
 */
#ifndef OC_BOOT_HANDOFF_H
#define OC_BOOT_HANDOFF_H

/* ---- ABI constants ------------------------------------------------ */
#define OCR_UEFI_HANDOFF_MAGIC 0x4F43524D45434C31ULL /* "OCRMECL1" */

/* The OCRMECL BIOS image layout (build-time contract of
 * ocrmecl/bios/build_ocr_image.py - the patch block the ocr-core
 * reads at linear 0x7DA0 after the stage1 sector):
 *   0x7C00 + 0x1A0  u32  magic 'OCRM' (0x4D52434F)
 *   0x7C00 + 0x1A4  u32  OCBL.BIN byte size (padded by the packer to
 *                          the exact image size it appended)
 *   0x7C00 + 0x1A8  u32  OCRMECL core byte size (stage1 + core, the
 *                          offset of OCBL.BIN inside the preload)
 *   0x7C00 + 0x1AC  u32  reserved (0)
 */
#define OCR_BIOS_PATCH_OFF   0x1A0
#define OCR_BIOS_PATCH_MAGIC 0x4D52434FUL /* 'OCRM' little-endian */

/* The OCRMECL BIOS low-memory tables (see the BIOS contract above):
 * all below 0x7C00, all survive the OCBL.BIN move. */
#define OCR_GDT_ADDR   0x1000        /* 5 entries, 40 bytes           */
#define OCR_FAULT_ADDR 0x1100        /* 32-bit hlt stub w/ serial '!' */
#define OCR_IDT_ADDR   0x2000        /* 256 gates, 2048 bytes         */
#define OCR_STUB_ADDR  0x6000        /* the move stub (transient)     */

/* ---- the UEFI handoff blob ---------------------------------------- */
#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct ocr_uefi_handoff {
    uint64_t magic;             /* OCR_UEFI_HANDOFF_MAGIC               */
    uint32_t version;           /* 1                                     */

    /* the RAM copy of the whole boot ISO (the only disk backend OCBL
     * has after the handoff - serve sectors from it) */
    uint64_t iso_base;          /* physical base of the ISO image       */
    uint64_t iso_size;          /* byte size                             */

    /* the relocated OCBL PE image (LoadImage already applied .reloc;
     * never StartImage'd - the raw entry IS this image's entry point) */
    uint64_t ocbl_base;         /* image base (LoadedImage->ImageBase)  */
    uint64_t ocbl_size;         /* image size                           */
    uint64_t ocbl_entry;        /* entry VA = base + AddressOfEntryRVA  */

    /* the GOP framebuffer captured before EBS (0 = no linear fb) */
    uint64_t fb_addr;
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_bpp;            /* 32 on the modes we pick              */
    uint8_t  fb_rgb;            /* 1 = BGRx (pixel format 1), 0 = RGBx  */

    /* ACPI (captured from the EFI configuration table before EBS) */
    uint64_t rsdp;              /* 0 = not found                        */

    /* the ExitBootServices memory map (descriptor layout per the UEFI
     * spec - descriptor_size includes the trailing pad) */
    uint64_t mmap;              /* pointer to the captured descriptors  */
    uint64_t mmap_size;         /* total bytes                          */
    uint64_t mmap_desc_size;    /* sizeof(efi_memory_descriptor)+pad    */
    uint32_t mmap_desc_version; /* 1                                    */

    /* OCRMECL's own serial state: COM1 115200 8N1 is already live and
     * every evidence line OCRMECL printed is on the wire - OCBL keeps
     * the same console discipline from its first byte on. */
    uint32_t serial_ok;         /* 1 = COM1 was initialized             */

    uint32_t _reserved[3];
} ocr_uefi_handoff_t;

/* the raw-entry ABI (both sides compile this header):
 *   __attribute__((ms_abi)) void ocbl_raw_entry(
 *       const ocr_uefi_handoff_t* h, uint64_t magic);
 * with h in RCX and magic in RDX (MS x64 first/second arguments).
 * A magic mismatch halts with a serial marker - the ABI is explicit,
 * not implicit. */
typedef __attribute__((ms_abi)) void (*ocbl_raw_entry_fn)(
    const ocr_uefi_handoff_t* h, uint64_t magic);

#endif /* __ASSEMBLER__ */
#endif /* OC_BOOT_HANDOFF_H */
