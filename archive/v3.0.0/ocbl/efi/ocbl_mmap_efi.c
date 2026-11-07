/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/ocbl_mmap_efi.c
 * Purpose: the UEFI flavor of the ocbl_mmap.h contract.  GetMemoryMap
 *          descriptors converted in place to the E820-style entries
 *          the shared mb2 tag-6 builder emits (the kernel's mem_pmm
 *          consumes the same 24-byte shape from GRUB).
 *
 *          The converted map lands in the same fixed low-memory
 *          buffer the BIOS flavor uses (0x60000, reserved at B5-3),
 *          so common/ocbl_load.c builds the tag untouched.
 *
 *          The GetMemoryMap map_key is remembered for ExitBootServices
 *          (B5-5): the key is only valid until the next boot-services
 *          allocation, so the caller re-runs ocbl_mmap_query() right
 *          before every EBS attempt.
 */
#include "efi.h"
#include "../common/ocbl_mmap.h"
#include "../common/ocbl_serial.h"

/* the raw descriptor buffer: descriptors are `desc_size` apart (48 on
 * OVMF/EDK2, 40 by the spec - the stride is honored at walk time);
 * the map for a busy fleet easily needs ~6.5 KiB (B5-5 live-caught:
 * the first call reported needing 6384 B) - 32 KiB is generous and
 * still nothing next to the reserved windows. */
#define RAW_MAX 640
static efi_memory_descriptor g_raw[RAW_MAX];
static u64 g_key;
static u64 g_desc_size;

static ocbl_mmap_t* g_map(void)
{
    return (ocbl_mmap_t*)OCBL_MMAP_BUF;
}

/* ---- EFI memory type -> E820 type (the only semantic table) ----
 *   Reserved/Loader/Runtime/MMIO/Pal      -> 2 reserved
 *   BootServices code+data -> 1 usable: ExitBootServices frees these
 *     BEFORE the kernel's mem_pmm consumes the tag - the exact same
 *     post-EBS snapshot GRUB hands over through the EFI mmap tag.
 *   Conventional            -> 1 usable
 *   Unusable                -> 5 defective
 *   ACPI reclaim            -> 4
 *   ACPI NVS                -> 3
 * (EFI type numbering per UEFI spec 7.2 - the corrected defines in
 *  efi.h; the historical ones there were off by one for LOADER/BS/RT.) */
static u32 to_e820(u32 t)
{
    switch (t) {
    case EFI_BOOT_SERVICES_CODE:
    case EFI_BOOT_SERVICES_DATA:
    case EFI_CONVENTIONAL_MEMORY:
        return 1;
    case EFI_UNUSABLE_MEMORY:
        return 5;
    case EFI_ACPI_RECLAIM_MEMORY:
        return 4;
    case EFI_ACPI_MEMORY_NVS:
        return 3;
    default: /* Reserved/Loader/RT/MMIO/Pal/Persistent/others */
        return 2;
    }
}

u32 ocbl_mmap_query(void)
{
    ocbl_mmap_t* m = g_map();

    /* GetMemoryMap is size-driven: first call learns the size, the
     * buffer here is big enough that a second round is not expected
     * (checked below). */
    u64 sz = sizeof(g_raw);
    u64 key = 0;
    u64 dsz = 0;
    u32 ver = 0;
    efi_status st = ocbl_efi_bs->get_memory_map(&sz, g_raw, &key, &dsz,
                                                &ver);
    if (st != EFI_SUCCESS) {
        ocbl_serial_puts("[efi] GetMemoryMap failed: 0x");
        ocbl_serial_hex((u64)st, 8);
        ocbl_serial_puts(" (need 0x");
        ocbl_serial_hex(sz, 8);
        ocbl_serial_puts(" have 0x");
        ocbl_serial_hex(sizeof(g_raw), 8);
        ocbl_serial_puts(")\r\n");
        return 0;
    }
    g_key = key;
    g_desc_size = dsz;

    u64 n = sz / dsz;
    if (n > RAW_MAX * sizeof(efi_memory_descriptor) / dsz)
        n = RAW_MAX * sizeof(efi_memory_descriptor) / dsz;

    /* convert + coalesce: the firmware already emits the descriptors
     * sorted by physical address, so adjacent same-type entries merge
     * into one E820 range and the 128-entry shared buffer always has
     * room (typical OVMF maps are ~40 descriptors -> ~25 ranges). */
    m->count = 0;
    for (u64 i = 0; i < n && m->count < OCBL_MMAP_MAX; i++) {
        efi_memory_descriptor* d = (efi_memory_descriptor*)
            ((u8*)g_raw + i * dsz);
        u64 len = d->number_of_pages * 4096ull;
        if (len == 0)
            continue;
        u32 t = to_e820(d->type);
        ocbl_mmap_entry_t* last = m->count ?
            &m->entries[m->count - 1] : 0;
        if (last && last->type == t &&
            last->addr + last->len == d->physical_start) {
            last->len += len;                 /* merge */
            continue;
        }
        m->entries[m->count].addr = d->physical_start;
        m->entries[m->count].len = len;
        m->entries[m->count].type = t;
        m->entries[m->count].reserved = 0;
        m->count++;
    }
    return m->count;
}

const ocbl_mmap_t* ocbl_mmap_get(void)
{
    return g_map();
}

/* The map_key from the last ocbl_mmap_query() - ExitBootServices wants
 * exactly this value, and it expires with every boot-services call
 * that touches memory. */
u64 ocbl_mmap_boot_key(void)
{
    return g_key;
}
