/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocbl/efi/ocbl_ramdisk.c
 * Purpose: the RAW flavor's ocbl_disk.h backend - the whole boot ISO
 *          as a single read-only RAM disk (the buffer OCRMECL read
 *          from the boot medium before ExitBootServices, handed over
 *          through interface/boot_handoff.h).
 *
 *          Post-EBS there is no firmware disk service, so the RAM
 *          copy is the ONLY drive the scanner/filesystem/loader
 *          layers ever see: drive 0, cdrom-class, 2048-byte sectors,
 *          reads served by plain memcpy.
 */
#include "efi.h"
#include "../common/ocbl_disk.h"
#include "../common/ocbl_serial.h"
#include "boot_handoff.h"

static const u8* g_iso;
static u64 g_sectors;

void ocbl_ramdisk_init(const ocr_uefi_handoff_t* h)
{
    g_iso = (const u8*)(u64)h->iso_base;
    g_sectors = h->iso_size / 2048;
    ocbl_serial_puts("[raw] ram disk: ISO ");
    ocbl_serial_dec(h->iso_size);
    ocbl_serial_puts(" bytes (");
    ocbl_serial_dec(g_sectors);
    ocbl_serial_puts(" sectors of 2048)\r\n");
}

/* ---- the ocbl_disk.h contract (one cdrom-class drive) ---- */

u32 ocbl_disk_enumerate(u8* drives, u32 max)
{
    if (max < 1)
        return 0;
    drives[0] = 0;
    return 1;
}

u8 ocbl_disk_kind(u8 drive)
{
    return drive == 0 ? 1 : 0;          /* cdrom-like, 2048-byte */
}

bool ocbl_disk_ext_ok(u8 drive)
{
    (void)drive;
    return true;
}

bool ocbl_disk_probe(u8 drive)
{
    return drive == 0 && g_iso != 0;
}

bool ocbl_disk_geom(u8 drive, u64* total_sectors, u16* bytes_per_sector)
{
    if (drive != 0)
        return false;
    if (total_sectors)
        *total_sectors = g_sectors;
    if (bytes_per_sector)
        *bytes_per_sector = 2048;
    return true;
}

bool ocbl_disk_read(u8 drive, u64 lba, u16 nsec, void* buf)
{
    if (drive != 0 || !g_iso || nsec == 0)
        return false;
    if (lba + nsec > g_sectors)
        return false;
    const u8* src = g_iso + lba * 2048;
    for (u64 i = 0; i < (u64)nsec * 2048; i++)
        ((u8*)buf)[i] = src[i];
    return true;
}

bool ocbl_disk_write(u8 drive, u64 lba, u16 nsec, const void* buf)
{
    (void)drive; (void)lba; (void)nsec; (void)buf;
    return false;                       /* the RAM ISO is read-only */
}
