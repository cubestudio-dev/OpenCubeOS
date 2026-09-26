/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/ata.h
 * Purpose: ATA/IDE PIO driver (LBA28) for the primary and secondary channels.
 *
 * Drives are addressed by a single integer 0..3:
 *   0 = primary master    (0x1F0, head 0xE0)
 *   1 = primary slave     (0x1F0, head 0xF0)
 *   2 = secondary master  (0x170, head 0xE0)
 *   3 = secondary slave   (0x170, head 0xF0)
 */
#ifndef OC_ATA_H
#define OC_ATA_H

#include "types.h"

void ata_init(void);

/* Read `count` 512-byte sectors starting at `lba` from `drive` into `buf`.
 * Returns the number of sectors read (== count) on success, or a negative
 * value on error. `buf` must be at least count*512 bytes. */
int ata_read_sectors(int drive, u64 lba, int count, void *buf);

/* Write `count` 512-byte sectors starting at `lba` to `drive` from `buf`.
 * Returns the number of sectors written on success, or a negative value. */
int ata_write_sectors(int drive, u64 lba, int count, const void *buf);

/* Returns 1 if the drive is present (responds to IDENTIFY with a non-erasing
 * signature), 0 otherwise. */
int ata_detect(int drive);

#endif /* OC_ATA_H */
