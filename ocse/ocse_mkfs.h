/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_mkfs.h
 * Purpose: the three OCSECL format commands:
 *   mkfs.fat32 <dev> confirm
 *   mkfs.exfat <dev> confirm
 *   mkfs.ext4 <dev> confirm
 *
 * The explicit `confirm` token is the BUG-0239 data-loss gate migrated
 * from the base blk_cmds.c mkfs.* commands: without it NOTHING is
 * written and the warning prints.
 *
 * FAT32/exFAT layouts are the battle-tested base implementations
 * (cluster sizing per the Microsoft table, FAT size by iteration to a
 * fixed point, reserved-area wipe). mkfs.ext4 is a REAL ext4 volume
 * (WP-06b scope: the base mkfs.ext4 wrote a bare superblock and
 * declared the rest): 4 KiB blocks, no journal (the ext2+extents
 * profile every e2fsprogs understands), group descriptors, block+inode
 * bitmaps, inode tables, root inode, lost+found - readable AND
 * writable by both our driver and Linux.
 *
 * After formatting, the MBR type byte of the partition (when the
 * device is a partition child "hd0pN") is updated to the canonical
 * value (FAT32 LBA=0x0C, exFAT=0x07, ext=0x83) - what mkfs on Linux
 * leaves for parted to see.
 */
#ifndef OCSE_MKFS_H
#define OCSE_MKFS_H

#include "types.h"

enum {
    OCSE_FMT_FAT32 = 0,
    OCSE_FMT_EXFAT,
    OCSE_FMT_EXT4,
};

/* Validate the command tail (device + literal `confirm`). On success
 * copy the device token into devbuf and return 0; on refusal print
 * the warning and return nonzero (nothing is written). */
int ocse_mkfs_gate(const char* cmd, const char* args,
                   char* devbuf, int devbuf_len);

/* Format dev_idx. Returns 0 on success. */
int ocse_mkfs_fat32(int dev_idx);
int ocse_mkfs_exfat(int dev_idx);
int ocse_mkfs_ext4(int dev_idx);

/* Sync the partition's MBR type byte with the fresh filesystem. */
void ocse_mkfs_sync_mbr_type(const char* dev_name, int fmt);

#endif /* OCSE_MKFS_H */
