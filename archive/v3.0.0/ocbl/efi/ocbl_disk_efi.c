/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/efi/ocbl_disk_efi.c
 * Purpose: the UEFI flavor of the ocbl_disk.h backend: whole-disk
 *          EFI_BLOCK_IO_PROTOCOL instances wrapped in the same
 *          contract the BIOS flavor serves from int 13h.
 *
 *          Only WHOLE-DISK handles are collected (LogicalPartition ==
 *          0): the common scanner decodes the partition tables itself
 *          (the B2b engine), so exposing the firmware's partition
 *          views would double-report every volume.
 *
 *          The drive number handed to common code is the index into
 *          this unit's handle table (0..N-1) - stable for the boot.
 */
#include "efi.h"
#include "../common/ocbl_disk.h"
#include "../common/ocbl_serial.h"

#define OCBL_EFI_MAX_DISKS 32

typedef struct {
    efi_handle handle;
    efi_block_io* io;
    u64 total_sectors;
    u32 block_size;
} ocbl_efi_disk_t;

static ocbl_efi_disk_t g_disks[OCBL_EFI_MAX_DISKS];
static u32 g_ndisks;

/* Enumerate whole-disk BlockIo instances.  Called once from
 * efi_entry before the scanner runs. */
void ocbl_efi_disk_init(void)
{
    /* Connect every controller first (B5-3 live-caught): the BDS
     * only connects the BOOT chain - booted from our El Torito UEFI
     * entry, exactly ONE BlockIo exists (the ESP partition view) and
     * the extra disks (the scan fleet!) never appear.  The UEFI Shell
     * looks "complete" only because it runs ConnectAll itself.  The
     * recursive ConnectController sweep over ALL handles is the
     * driver way to reach the same state. */
    {
        u64 nh = 0;
        efi_handle* hb = 0;
        efi_status stc = ocbl_efi_bs->locate_handle_buffer(
            0 /* AllHandles */, 0, 0, &nh, &hb);
        if (stc == EFI_SUCCESS && hb && nh) {
            u64 connected = 0;
            for (u64 i = 0; i < nh; i++) {
                efi_status one = ocbl_efi_bs->connect_controller(
                    hb[i], 0, 0, 1 /* Recursive */);
                if (one == EFI_SUCCESS)
                    connected++;
            }
            ocbl_efi_bs->free_pool(hb);
            ocbl_serial_puts("[efi] connect sweep: ");
            ocbl_serial_dec(connected);
            ocbl_serial_puts(" controllers\r\n");
        }
    }

    u64 nr = 0;
    efi_handle* buf = 0;
    /* UEFI spec: for ByProtocol the SearchKey MUST be the protocol
     * GUID pointer (passing NULL yields an empty handle list - found
     * live: "BlockIo whole-disk handles: 0" with the DVD attached) */
    efi_status st = ocbl_efi_bs->locate_handle_buffer(
        2 /* ByProtocol */, &ocbl_efi_block_io_guid,
        (void*)&ocbl_efi_block_io_guid, &nr, &buf);
    if (st != EFI_SUCCESS || !buf) {
        ocbl_serial_puts("[efi] BlockIo: none found\r\n");
        return;
    }
    for (u64 i = 0; i < nr && g_ndisks < OCBL_EFI_MAX_DISKS; i++) {
        /* minimal clean probe: HandleProtocol then the media facts */
        efi_block_io* io = 0;
        st = ocbl_efi_bs->handle_protocol(buf[i], &ocbl_efi_block_io_guid,
                                         (void**)&io);
        if (st != EFI_SUCCESS || !io || !io->media)
            continue;
        if (io->media->logical_partition)
            continue;               /* partition view: skip */
        if (!io->media->media_present)
            continue;
        if (io->media->block_size == 0)
            continue;
        g_disks[g_ndisks].handle = buf[i];
        g_disks[g_ndisks].io = io;
        g_disks[g_ndisks].block_size = io->media->block_size;
        g_disks[g_ndisks].total_sectors = io->media->last_block + 1;
        g_ndisks++;
    }
    ocbl_efi_bs->free_pool(buf);
    ocbl_serial_puts("[efi] BlockIo whole-disk handles: ");
    ocbl_serial_dec(g_ndisks);
    ocbl_serial_puts("\r\n");
}

/* ---- the ocbl_disk.h contract ---- */

u32 ocbl_disk_enumerate(u8* drives, u32 max)
{
    u32 n = g_ndisks < max ? g_ndisks : max;
    for (u32 i = 0; i < n; i++)
        drives[i] = (u8)i;
    return n;
}

u8 ocbl_disk_kind(u8 drive)
{
    if (drive >= g_ndisks)
        return 0;
    /* the El Torito / ATAPI class exposes 2048-byte sectors; virtio
     * and SATA disks are 512 (the same class split the BIOS flavor
     * reads off drive number 0xE0+) */
    return g_disks[drive].block_size >= 2048 ? 1 : 0;
}

bool ocbl_disk_ext_ok(u8 drive)
{
    (void)drive;
    return true;                    /* no int 13h legacy question here */
}

bool ocbl_disk_probe(u8 drive)
{
    return drive < g_ndisks;
}

bool ocbl_disk_geom(u8 drive, u64* total_sectors, u16* bytes_per_sector)
{
    if (drive >= g_ndisks)
        return false;
    if (total_sectors)
        *total_sectors = g_disks[drive].total_sectors;
    if (bytes_per_sector)
        *bytes_per_sector = (u16)g_disks[drive].block_size;
    return true;
}

bool ocbl_disk_read(u8 drive, u64 lba, u16 nsec, void* buf)
{
    if (drive >= g_ndisks || nsec == 0)
        return false;
    efi_block_io* io = g_disks[drive].io;
    /* ReadBlocks wants byte counts and honors the media id; UEFI has
     * no below-1-MiB restriction, so the common loader paths (the
     * 0x30000 bounce window is a BIOS-only concern) can read the
     * kernel straight to its link address in B5-5. */
    efi_status st = io->read_blocks(io, io->media->media_id, lba,
                                    (u64)nsec * g_disks[drive].block_size,
                                    buf);
    return st == EFI_SUCCESS;
}
