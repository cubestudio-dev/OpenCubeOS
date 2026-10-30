/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_disk.c
 * Purpose: int 13h disk services via the real-mode trampoline.
 *
 *          Sector size discipline (found live in B2a): SeaBIOS exposes
 *          the no-emulation El Torito boot drive with 2048-BYTE sectors
 *          (a CD address space in AH=42h LBAs), while hard disks use the
 *          classic 512.  Every caller goes through ocbl_disk_geom() to
 *          learn the drive's sector size before issuing reads.
 */
#include "ocbl_disk.h"
#include "ocbl_bios.h"
#include "ocbl_serial.h"

bool ocbl_disk_ext_ok(u8 drive)
{
    ocbl_bios_regs_t r;
    for (int i = 0; i < 3; i++) {           /* the classic BX handshake */
        r.eax = 0x4100;
        r.ebx = 0x55AA;
        r.edx = (u16)drive;
        r.ecx = 0;
        r.esi = r.edi = 0;
        r.es = r.ds = 0;
        ocbl_bios_int(0x13, &r);
        if ((r.ebx & 0xFFFF) == 0xAA55)
            return true;
        /* reset + retry once (old-BIOS patience) */
        ocbl_bios_regs_t z = {0};
        z.edx = (u16)drive;
        ocbl_bios_int(0x13, &z);
    }
    return false;
}

bool ocbl_disk_read(u8 drive, u64 lba, u16 nsec, void* buf)
{
    u64 addr = (u64)(u32)(u64)buf;
    if (addr >= 0x100000) return false;    /* real-mode DAP limit */
    u32 seg = (u32)(addr >> 4);
    u32 off = (u32)(addr & 0xF);

    /* the trampoline DAP area (0x40500 = segment 0x4000, offset 0x500).
     * DAP BYTE layout (u16 view - the classic mis-mapping gotcha,
     * live-caught in B2a: block count is u16[1], NOT u16[2], or the
     * BIOS "transfers" zero blocks and reports success):
     *   bytes 0-1   size(0x10) + reserved      -> dap[0]
     *   bytes 2-3   block count                -> dap[1]
     *   bytes 4-5   buffer offset              -> dap[2]
     *   bytes 6-7   buffer segment             -> dap[3]
     *   bytes 8-15  LBA (u64)                  -> dap[4..7] */
    volatile u16* dap = (volatile u16*)OCBL_TRAMP_DAP;
    dap[0] = 0x0010;                       /* size + reserved */
    dap[1] = nsec;                         /* block count */
    dap[2] = (u16)off;                     /* target offset */
    dap[3] = (u16)seg;                     /* target segment */
    dap[4] = (u16)(lba & 0xFFFF);
    dap[5] = (u16)((lba >> 16) & 0xFFFF);
    dap[6] = (u16)((lba >> 32) & 0xFFFF);
    dap[7] = (u16)((lba >> 48) & 0xFFFF);

    ocbl_bios_regs_t r;
    r.eax = 0x4200;
    r.edx = (u16)drive;
    r.esi = 0x0500;                         /* DAP at 0x4000:0x0500 */
    r.ds = 0x4000;
    r.es = 0;
    r.edi = 0;
    r.ebx = 0;
    r.ecx = 0;
    if (ocbl_bios_int(0x13, &r) != 0) {
        /* one reset + retry, then give up honestly */
        ocbl_bios_regs_t z = {0};
        z.edx = (u16)drive;
        ocbl_bios_int(0x13, &z);
        if (ocbl_bios_int(0x13, &r) != 0)
            return false;
    }
    return true;
}

bool ocbl_disk_geom(u8 drive, u64* total_sectors, u16* bytes_per_sector)
{
    /* AH=48h extensions geometry into the trampoline scratch (0x40200) */
    volatile u16* g = (volatile u16*)OCBL_TRAMP_SCRATCH;
    for (int i = 0; i < 20; i++)
        g[i] = 0;
    g[0] = 30;                             /* structure size */
    ocbl_bios_regs_t r;
    r.eax = 0x4800;
    r.edx = (u16)drive;
    r.ds = 0x4000;
    r.esi = 0x0600;
    r.es = 0;
    r.edi = 0;
    r.ebx = 0;
    r.ecx = 0;
    if (ocbl_bios_int(0x13, &r) != 0)
        return false;
    /* AH=48h "device parameters" layout (u16 view):
     *   g[0] size, g[1] info flags, g[2..3] cylinders,
     *   g[4..5] heads, g[6..7] sectors/track,
     *   g[8..11] total addressable sectors (QWORD),
     *   g[12] bytes per sector */
    u64 total = (u64)g[8] | ((u64)g[9] << 16) | ((u64)g[10] << 32) |
                ((u64)g[11] << 48);
    u16 bps = g[12];
    if (bps < 512 || total == 0)
        return false;
    if (total_sectors)
        *total_sectors = total;
    if (bytes_per_sector)
        *bytes_per_sector = bps;
    return true;
}

bool ocbl_disk_probe(u8 drive)
{
    ocbl_bios_regs_t r;
    r.eax = 0x0800;
    r.edx = (u16)drive;
    r.es = r.ds = 0;
    r.ecx = r.ebx = r.esi = r.edi = 0;
    u32 cf = ocbl_bios_int(0x13, &r);
    if (cf != 0) {
        /* CHS probe failed; extensions presence is the real gate */
        return ocbl_disk_ext_ok(drive);
    }
    return true;
}
