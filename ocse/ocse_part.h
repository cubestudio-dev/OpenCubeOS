/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_part.h
 * Purpose: partition editing for OCSECL - the six disk commands:
 *   lsdisk            list install-target disks
 *   lspart <disk>     list partitions with fs classification
 *   parted <disk>     show the raw table (MBR slots / GPT entries)
 *   mkpart <disk> <start> <end>   create a partition (MiB, 1-based
 *                     alignment at LBA 2048, never touching existing
 *                     partitions - overlaps are refused)
 *   rmpart <disk> <n> delete partition #n (MBR slot / GPT entry)
 *   setflag <disk> <n> <flag>   set/clear boot (MBR 0x80) or the GPT
 *                     legacy-boot attribute
 *
 * Rules enforced here (the WP-06b contract):
 *  - Existing partitions are never moved or resized: mkpart only
 *    carves space the table reports free, and refuses overlaps.
 *  - A full disk reports "no free space" - the user must clean or
 *    shrink manually (shrinking is deliberately NOT implemented:
 *    silently shrinking a foreign partition is data loss).
 *  - MBR writes go through read-modify-write of sector 0 so boot
 *    flags, CHS fillers and the disk signature survive.
 *  - GPT edits rewrite the entry array + header CRCs at both the
 *    primary and backup locations.
 *
 * New/removed partitions are registered/unregistered as
 * "<disk>pN" block devices so mkfs/mount address them immediately
 * without a reboot.
 */
#ifndef OCSE_PART_H
#define OCSE_PART_H

#include "types.h"

/* All return 0 on success, nonzero on failure (command exit code). */
int ocse_part_lsdisk(void);
int ocse_part_lspart(const char* disk);
int ocse_part_parted(const char* disk);
int ocse_part_mkpart(const char* disk, u64 start_mib, u64 end_mib);
int ocse_part_rmpart(const char* disk, int part_no);
int ocse_part_setflag(const char* disk, int part_no,
                      const char* flag, int value);

/* Resolve "hd0"-style disk name to a block-device index; refuses
 * optical drives (cdN) and partition children (hd0pN). Returns the
 * index or -1. */
int ocse_part_disk_idx(const char* disk);

#endif /* OCSE_PART_H */
