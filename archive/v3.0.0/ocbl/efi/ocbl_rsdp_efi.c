/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/ocbl_rsdp_efi.c
 * Purpose: the UEFI flavor of ocbl_rsdp_find(): the firmware hands
 *          us the RSDP pointer in the EFI configuration table (ACPI
 *          2.0 GUID first, ACPI 1.0 GUID as the fallback) - no
 *          memory scan needed (the BIOS flavor scans the EBDA and
 *          0xE0000 in bios/ocbl_rsdp_bios.c).
 */
#include "efi.h"
#include "../common/ocbl_boot.h"

static int guid_eq(const efi_guid* a, const efi_guid* b)
{
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    for (int i = 0; i < 16; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

u64 ocbl_rsdp_find(void)
{
    if (!ocbl_efi_st || !ocbl_efi_st->config_table)
        return 0;
    const efi_config_table* t =
        (const efi_config_table*)ocbl_efi_st->config_table;
    u64 found = 0;
    for (u64 i = 0; i < ocbl_efi_st->nr_table_entries; i++) {
        if (guid_eq(&t[i].vendor_guid, &ocbl_efi_acpi20_guid)) {
            found = (u64)t[i].vendor_table;
            break;                  /* ACPI 2.0 wins immediately */
        }
        if (guid_eq(&t[i].vendor_guid, &ocbl_efi_acpi10_guid) && !found)
            found = (u64)t[i].vendor_table;
    }
    return found;
}
