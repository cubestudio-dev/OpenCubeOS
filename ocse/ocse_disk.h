/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_disk.h
 * Purpose: OCSE disk identification: enumerate the block devices,
 * parse MBR/GPT tables and classify each partition by filesystem
 * signature (NTFS / exFAT / FAT32 / ext). Also flags disks whose
 * capacity is below the installer minimum (the "low space" notice).
 */
#ifndef OCSE_DISK_H
#define OCSE_DISK_H

#include "types.h"

#define OCSE_MAX_DISKS  8
#define OCSE_MAX_PARTS  16

#define OCSE_MIN_DISK_MB 64     /* installer minimum; below = low-space */

typedef enum {
    OCSE_FS_UNKNOWN = 0,
    OCSE_FS_FAT32,
    OCSE_FS_EXFAT,
    OCSE_FS_NTFS,
    OCSE_FS_EXT,        /* ext2/3/4 (superblock magic 0xEF53) */
    OCSE_FS_ESP,        /* EFI system partition (GPT GUID) */
    OCSE_FS_SWAP,       /* Linux swap (MBR 0x82) */
} ocse_fs_t;

typedef struct {
    ocse_fs_t fs;
    u64 start_lba;
    u64 sectors;
    u64 mb;
    u8  mbr_type;       /* MBR type byte (0 for GPT) */
} ocse_part_t;

typedef struct {
    int  present;
    char name[16];
    u64  sectors;
    u64  mb;
    int  table_kind;    /* 0 none, 1 MBR, 2 GPT */
    int  nparts;
    ocse_part_t parts[OCSE_MAX_PARTS];
    int  space_low;     /* capacity < OCSE_MIN_DISK_MB */
} ocse_disk_t;

extern ocse_disk_t g_ocse_disks[OCSE_MAX_DISKS];
extern int g_ocse_ndisks;

/* Rescan everything (called at boot, on GUI re-check and by `disks`). */
void ocse_disk_scan(void);

/* Short ASCII classifier name ("NTFS", "exFAT", "FAT32", "ext", "ESP",
 * "swap", "?"). */
const char* ocse_fs_name(ocse_fs_t fs);

#endif /* OCSE_DISK_H */
