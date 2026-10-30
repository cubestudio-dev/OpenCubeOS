/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_fs.h
 * Purpose: read-only volume layer for the OCBL scanner: FAT12/16/32
 *          (the OCOS system volume, the OCIRE 0xEE shadow, the ESP)
 *          and ISO9660 (the OCSE install media).
 *
 *          The scanner only needs THREE operations per volume:
 *            - mount/detect from a block device range
 *            - stat a path (existence + size)
 *            - read a file into a caller buffer
 *          Everything is bounded: one shared 2 KiB sector window per
 *          call chain (OCBL_FS_SECBUF), no heap, no allocation.
 *
 *          LFN (long file names) are READ for FAT - the fixed paths
 *          the scanner probes are pure ASCII, but directory iteration
 *          for the "other Linux" EFI-vendor sweep needs the real
 *          names (EFIMICRO~1 style 8.3 mangles would still work, the
 *          LFN pass just makes the labels honest).
 */
#ifndef OCBL_FS_H
#define OCBL_FS_H

#include "ocbl_types.h"

#define OCBL_FS_SECBUF   0x62000u   /* 2 KiB shared sector window */
#define OCBL_FS_FATBUF   0x62800u   /* FAT table cache, 128 KiB max */

typedef enum {
    OCBL_VOL_NONE = 0,
    OCBL_VOL_FAT12,
    OCBL_VOL_FAT16,
    OCBL_VOL_FAT32,
    OCBL_VOL_ISO9660,
} ocbl_voltype_t;

/* A mounted read-only volume.  `drive` is the BIOS/UEFI block device
 * number, base_lba/sector_size the block window (the partition start
 * or 0 for a whole-device filesystem). */
typedef struct ocbl_volume {
    u8  drive;
    u8  fs;                    /* ocbl_voltype_t */
    u64 base_lba;
    u32 sector_size;           /* 512 (FAT) or 2048 (ISO9660) */
    /* FAT internals (also the mount fingerprint): */
    u16 bytes_per_sector;
    u8  sec_per_clus;
    u16 reserved_sectors;
    u8  nfats;
    u32 fat_sectors;           /* per FAT */
    u32 root_cluster;          /* FAT32; 0 for FAT12/16 */
    u16 root_entries;          /* FAT12/16 */
    u64 data_start;            /* cluster-2 base, absolute LBA */
    u64 total_sectors;
    /* ISO9660 internals: */
    u64 iso_root_lba;
    u32 iso_root_size;
} ocbl_volume_t;

/* FAT directory search result (also used by the EFI vendor sweep). */
#define OCBL_FS_NAME_MAX   64
typedef struct ocbl_fs_dirent {
    char  name[OCBL_FS_NAME_MAX];   /* NUL-terminated, LFN if present */
    u8    attr;                     /* FAT attrs, 0x10 = directory */
    u32   cluster;
    u32   size;
} ocbl_fs_dirent_t;

/* Try to mount the block range as one of the supported filesystems.
 * Returns true and fills `v` on success (v->fs != OCBL_VOL_NONE). */
bool ocbl_fs_mount(u8 drive, u64 base_lba, u32 sector_size,
                   ocbl_volume_t* v);

/* Does `path` exist?  Fills `size` when non-NULL. */
bool ocbl_fs_stat(const ocbl_volume_t* v, const char* path, u32* size);

/* Read a file (up to `max` bytes) into `buf`.  Returns the byte count
 * read, or 0 on any failure.  File bigger than max is truncated. */
u32 ocbl_fs_read_file(const ocbl_volume_t* v, const char* path,
                      void* buf, u32 max);

/* Positional read: `len` bytes of `path` starting at byte offset
 * `off` into `buf`.  Returns the byte count read (0 on failure).
 * The loader paths need this - reading a 2 MiB kernel through
 * read_file-at-offset-zero would be quadratic. */
u32 ocbl_fs_pread(const ocbl_volume_t* v, const char* path, u32 off,
                  void* buf, u32 len);

/* List a directory's direct entries.  Returns the entry count (0 on
 * failure or empty).  `out` holds up to `max` entries.  FAT only -
 * ISO9660 directories are probed by stat, not listed. */
u32 ocbl_fs_list_dir(const ocbl_volume_t* v, const char* path,
                     ocbl_fs_dirent_t* out, u32 max);

/* Short fingerprint for diagnostics: e.g. "FAT32 hd1:2048". */
void ocbl_fs_describe(const ocbl_volume_t* v, char* out, u32 max);

#endif /* OCBL_FS_H */
