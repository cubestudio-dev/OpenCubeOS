/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocbl/efi/ocbl_mmap_efi.c
 * Purpose: the RAW flavor of the ocbl_mmap.h contract.  The memory
 *          map OCRMECL captured at ExitBootServices time (handoff
 *          fields mmap/mmap_size/mmap_desc_size/mmap_desc_version,
 *          interface/boot_handoff.h) is converted in place to the
 *          E820-style entries the shared mb2 tag-6 builder emits.
 *
 *          The converted map lands in the same fixed low-memory
 *          buffer the BIOS flavor uses (0x60000, reserved by OCRMECL
 *          before EBS), so common/ocbl_load.c builds the tag
 *          untouched.  Post-EBS the captured map is stable by
 *          definition - no query loop, no map key, one conversion.
 */
#include "ocbl_efi_def.h"
#include "../common/ocbl_mmap.h"
#include "../common/ocbl_serial.h"
#include "boot_handoff.h"

static ocbl_mmap_t* g_map(void)
{
    return (ocbl_mmap_t*)OCBL_MMAP_BUF;
}

/* ---- EFI memory type -> E820 type (unchanged semantics) ----
 *   Reserved/Loader/Runtime/MMIO/Pal      -> 2 reserved
 *   BootServices code+data -> 1 usable (freed by ExitBootServices,
 *     which has ALREADY happened when this runs)
 *   Conventional            -> 1 usable
 *   Unusable                -> 5 defective
 *   ACPI reclaim            -> 4
 *   ACPI NVS                -> 3 */
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
    default:
        return 2;
    }
}

/* Convert the OCRMECL-captured map into the shared E820 buffer.
 * Returns the entry count (0 on a contract violation). */
u32 ocbl_mmap_from_captured(const ocr_uefi_handoff_t* h)
{
    ocbl_mmap_t* m = g_map();
    if (!h->mmap || !h->mmap_desc_size || !h->mmap_size)
        return 0;
    u64 n = h->mmap_size / h->mmap_desc_size;

    m->count = 0;
    for (u64 i = 0; i < n && m->count < OCBL_MMAP_MAX; i++) {
        const efi_memory_descriptor* d =
            (const efi_memory_descriptor*)(h->mmap + i * h->mmap_desc_size);
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
