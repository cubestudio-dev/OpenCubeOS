/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_ata.h
 * Purpose: ATA/ATAPI PIO driver for the OCIRE recovery kernel.
 *
 * Scope (WP-02): the recovery kernel must READ the OCIRE FAT12
 * volume and the OCOS FAT32 volume, and WRITE during repair
 * (system-file restore / reformat) and mailbox updates.  QEMU's
 * pc machine exposes the test disks on the two classic IDE buses;
 * real recovery media on mainstream PCs is IDE/SATA-AHCI-compat
 * the same way.  The driver is polling PIO with generous timeouts -
 * OCIRE is a polling kernel by design.
 *
 * Independent implementation: OCIRE shares no source with the OCOS
 * ATA driver (WP-01 independence rule); the register semantics are
 * the ATA/ATAPI-7 standard ones.
 */

#ifndef OCIRE_ATA_H
#define OCIRE_ATA_H

#include "ocire_types.h"

#define OCIRE_ATA_MAX_DEVS 4

typedef struct {
    int  present;          /* identify succeeded */
    int  bus;              /* 0 = primary (0x1F0), 1 = secondary (0x170) */
    int  slave;            /* 0 = master, 1 = slave */
    u64  sectors;          /* user-addressable sectors (LBA28 count) */
    char name[16];         /* model string, trimmed */
} ocire_ata_dev_t;

/* Probe both buses, master + slave.  Fills the internal table
 * (see ocire_ata_dev).  Returns the number of disks found. */
int ocire_ata_scan(void);

/* Device by scan index (0..ocire_ata_scan()-1), or NULL. */
const ocire_ata_dev_t *ocire_ata_dev(int idx);

/* Read `count` sectors (512 B each) at LBA `lba` from disk `idx`
 * into `buf`.  Returns 0 on success, negative on error. */
int ocire_ata_read(int idx, u64 lba, u32 count, void *buf);

/* Write `count` sectors at LBA `lba` from `buf` to disk `idx`.
 * Returns 0 on success, negative on error. */
int ocire_ata_write(int idx, u64 lba, u32 count, const void *buf);

#endif /* OCIRE_ATA_H */
