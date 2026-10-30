/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_acpi.c
 * Purpose: RSDP discovery for the multiboot2 ACPI tags and the ACPI
 *          S5 power-off path (B4b wires the actual SLP_TYP write;
 *          this unit only finds and validates the pointer).
 *
 * Scan order (ACPI spec): the EBDA (word at 0x40E, 1 KiB), then the
 * BIOS read-only area 0xE0000-0xFFFFF.  Validation: "RSD PTR "
 * signature + checksum over the 20-byte (1.0) or 36-byte (2.0+)
 * structure.
 */
#include "ocbl_boot.h"
#include "ocbl_types.h"

static u8 chk_sum(const volatile u8* p, u32 len)
{
    u8 s = 0;
    for (u32 i = 0; i < len; i++)
        s = (u8)(s + p[i]);
    return s;
}

static u64 probe(u32 start, u32 end)
{
    volatile u8* p = (volatile u8*)(u64)start;
    for (u32 off = 0; off + 20 <= end - start; off += 16) {
        volatile u8* q = p + off;
        if (q[0] != 'R' || q[1] != 'S' || q[2] != 'D' ||
            q[3] != ' ' || q[4] != 'P' || q[5] != 'T' ||
            q[6] != 'R' || q[7] != ' ')
            continue;
        u8 rev = q[15];
        if (rev == 0) {
            if (chk_sum(q, 20) != 0)
                continue;
        } else {
            u32 len = (u32)q[20] | ((u32)q[21] << 8) |
                      ((u32)q[22] << 16) | ((u32)q[23] << 24);
            if (len != 36 || chk_sum(q, 36) != 0)
                continue;
        }
        return (u64)(u32)(start + off);
    }
    return 0;
}

u64 ocbl_rsdp_find(void)
{
    /* EBDA first (fixed-address read goes through the asm peek to
     * dodge -Warray-bounds false positives) */
    u16 ebda_seg = ocbl_peek16(0x40E);
    if (ebda_seg >= 0x8000 && ebda_seg < 0xA000) {
        u32 base = (u32)ebda_seg << 4;
        u64 r = probe(base, base + 1024);
        if (r)
            return r;
    }
    return probe(0xE0000, 0x100000);
}
