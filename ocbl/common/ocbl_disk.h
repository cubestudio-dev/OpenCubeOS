/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl */
#ifndef OCBL_DISK_H
#define OCBL_DISK_H

#include "ocbl_types.h"

/* Flavor note (B5-2): the drive number is the FLAVOR's disk index -
 * BIOS int 13h numbers (0x80.., 0xE0..) in the BIOS flavor, plain
 * BlockIo handle indexes in the UEFI flavor.  The common scanner and
 * filesystem layers never care which. */

/* Enumerate the disks the scanner should sweep.  Fills `drives` with
 * up to `max` flavor disk numbers and returns the count found (the
 * call is also the presence gate - absent disks are not listed). */
u32 ocbl_disk_enumerate(u8* drives, u32 max);

/* 0 = hard-disk-like, 1 = cdrom-like (2048-byte sectors).  Used for
 * the entry labels ("hd0"/"cd1") and the ISO9660 sector-size gate. */
u8 ocbl_disk_kind(u8 drive);

/* int 13h extensions presence for a BIOS drive (AH=41h). */
bool ocbl_disk_ext_ok(u8 drive);

/* Read `nsec` sectors starting at LBA `lba` of `drive` into `buf`.
 * REAL-MODE CONSTRAINT: the DAP target is segment:offset, so `buf`
 * must be a linear address BELOW 1 MiB (the bounce discipline for
 * high payloads - kernel images - belongs to the loader layer, which
 * chunks through the low bounce buffer).  LBA units are the drive's
 * own sector size (learn it via ocbl_disk_geom first).  Returns true
 * on success. */
bool ocbl_disk_read(u8 drive, u64 lba, u16 nsec, void* buf);

/* Query geometry (AH=48h extensions): total sectors + bytes/sector.
 * Returns true on success. */
bool ocbl_disk_geom(u8 drive, u64* total_sectors, u16* bytes_per_sector);

/* Is a BIOS drive present (AH=08h / extensions probe)? */
bool ocbl_disk_probe(u8 drive);

#endif /* OCBL_DISK_H */
