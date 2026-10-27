/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_fat.h
 * Purpose: FAT12 + FAT32 volume access for the OCIRE recovery kernel.
 *
 * WP-02 split of duties (why both variants):
 *
 *   FAT12 - the OCIRE recovery volume (the 0xEE shadow partition on
 *           the OCIRE disk): READ the boot kernel, the golden image,
 *           the log-agent ELF and the boot log.  The mailbox and the
 *           log are fixed-size files whose DATA SECTORS are patched
 *           in place (ocire_fat_file_patch) - the FAT chain and the
 *           directory entry never change, so no FAT12 write support
 *           is needed (kept out deliberately: less recovery-kernel
 *           code touching its own volume).
 *
 *   FAT32 - the OCOS system volume (the repair TARGET): read for
 *           syscheck, and the FULL write set for repair: overwrite a
 *           file, create a file (with FAT chain allocation), create
 *           directories, and format the volume (the `repair all`
 *           reformat).  The BPB geometry written by ocire_fat_format
 *           mirrors the OCOS mkfs.fat32 command so a repaired volume
 *           mounts on both sides.
 *
 * The implementation is independent of the OCOS fs_fat32.c driver
 * (WP-01 rule); only 8.3 short names are handled (LFN entries are
 * skipped) - every file the recovery volume carries is 8.3 upper.
 */

#ifndef OCIRE_FAT_H
#define OCIRE_FAT_H

#include "ocire_types.h"

#define OCIRE_FAT_MAX_FILE   (192 * 1024)   /* largest file held in memory */
#define OCIRE_FAT_NAME_LEN   13             /* 8 + 1 + 3 + NUL */

typedef struct {
    int  ata_idx;          /* disk (ocire_ata_dev index) */
    u64  part_lba;         /* volume start sector (absolute) */
    int  fat_type;         /* 12 or 32 */
    u32  bytes_per_sec;
    u32  sec_per_clus;
    u32  reserved_secs;
    u32  nfats;
    u32  fat_secs;         /* size of ONE FAT */
    u64  fat_start;        /* volume-relative LBA of first FAT */
    u64  data_start;       /* volume-relative LBA of data region */
    u32  total_clusters;   /* including the 2 reserved ones */
    u32  free_hint;        /* alloc_cluster scans from here (mount/
                              format set it to 2).  Keeps sequential
                              allocations O(1) instead of rescanning
                              the whole FAT per cluster - the WP-02
                              repair copies allocate thousands of
                              clusters in a row. */
    u32  root_cluster;     /* FAT32 only */
    u64  root_dir_sectors; /* FAT12: absolute sector count of the root
                              directory region; FAT32: 0 */
    u32  cluster_secs;     /* sec_per_clus (kept separate for clarity) */
} ocire_fat_vol_t;

/* One open/looked-up file. */
typedef struct {
    ocire_fat_vol_t *vol;
    u32  first_cluster;
    u32  dir_cluster;      /* 0 = root (FAT12 fixed area / FAT32 root
                              cluster chain) */
    u64  dir_entry_sector; /* absolute sector holding the 32-byte entry */
    u32  dir_entry_off;    /* byte offset of the entry in that sector */
    u64  size;             /* file size in bytes */
    u64  data_lba;         /* absolute LBA of first data sector
                              (contiguity NOT assumed except where a
                              caller states it) */
    char name[OCIRE_FAT_NAME_LEN];
} ocire_fat_file_t;

/* Mount the volume at part_lba on disk ata_idx.  Returns 0 and fills
 * vol, negative on error (bad BPB / unsupported FAT16). */
int ocire_fat_mount(ocire_fat_vol_t *vol, int ata_idx, u64 part_lba);

/* Look up "PATH/TO/NAME" (8.3 upper-case, '/'-separated, no leading
 * slash) under the volume root.  Returns 0 and fills out, -1 when not
 * found, -2 on I/O or malformed directory. */
int ocire_fat_lookup(ocire_fat_vol_t *vol, const char *path,
                     ocire_fat_file_t *out);

/* Read the whole file into buf (cap bytes).  Returns bytes read, or
 * negative on error. */
s64 ocire_fat_read_file(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                        u8 *buf, u64 cap);

/* Overwrite an existing file with `len` bytes from buf.  If the size
 * differs the file is re-created (old chain freed, new one
 * allocated).  FAT32 only.  Returns 0 on success. */
int ocire_fat_write_file(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                         const u8 *buf, u64 len);

/* Create (or overwrite) path with buf/len; parent directories must
 * exist (ocire_fat_mkdir creates them).  FAT32 only.  Returns 0. */
int ocire_fat_create_file(ocire_fat_vol_t *vol, const char *path,
                          const u8 *buf, u64 len);

/* Create directory path (no-op when it already exists).  FAT32
 * only.  Returns 0. */
int ocire_fat_mkdir(ocire_fat_vol_t *vol, const char *path);

/* Format the volume at part_lba (FAT32, geometry like the OCOS
 * mkfs.fat32: cluster size by volume size, 32 reserved sectors, 2
 * FATs, empty root cluster) and re-mount vol on it.  Returns 0. */
int ocire_fat_format(ocire_fat_vol_t *vol, int ata_idx, u64 part_lba,
                     u64 part_sectors, const char *label);

/* Fixed-size in-place patch (FAT12 mailbox / log): copy `len` bytes
 * from buf into the file data at byte offset `off` (within the file
 * bounds; clusters are walked, contiguity NOT assumed).  Returns 0. */
int ocire_fat_file_patch(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                         u64 off, const void *buf, u32 len);

/* Absolute LBA of a file's FIRST data sector (cluster 2 of its
 * chain).  Callers that need the whole chain should walk it via
 * ocire_fat_read_file; this is for the fixed-layout bootstrap facts
 * (mailbox / log ring start LBA passed through the kexec cmdline). */
u64 ocire_fat_file_first_lba(ocire_fat_vol_t *vol, const ocire_fat_file_t *f);

/* Streaming helpers for the recovery workloads (syscheck / repair),
 * added in WP-02: the OCOS kernel (~1.4 MiB) and the GRUB font
 * (~2.4 MiB) exceed OCIRE_FAT_MAX_FILE, so integrity checks and
 * repairs must never buffer a whole file. */

/* Read `len` bytes of the file starting at byte offset `off` into
 * buf.  Returns the byte count read (short only at EOF) or negative
 * on I/O error. */
s64 ocire_fat_read_range(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                         u64 off, u8 *buf, u64 len);

/* Stream a file through SHA-256 without buffering it: digest gets
 * the 32-byte result, *size_out (may be NULL) the byte count
 * hashed.  Returns 0 or negative on I/O error. */
int ocire_fat_hash_file(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                        u8 digest[32], u64 *size_out);

/* Copy `src` (any mounted volume, e.g. the FAT12 golden image) to
 * `dst_path` (FAT32 target volume) in `chunk_cap`-sized pieces -
 * the repair path for files larger than OCIRE_FAT_MAX_FILE.
 * Creates or overwrites the destination.  `chunk` is caller-owned
 * scratch (>= 512 bytes).  *written_out (may be NULL) receives the
 * byte count.  Returns 0 or negative. */
int ocire_fat_copy_stream(ocire_fat_vol_t *svol, const ocire_fat_file_t *src,
                          ocire_fat_vol_t *dvol, const char *dst_path,
                          u8 *chunk, u32 chunk_cap, u64 *written_out);

/* Sector-level read/write on the volume's parent disk (absolute
 * LBA).  Exposed for the mailbox path (read-modify-write). */
int ocire_fat_read_sect(ocire_fat_vol_t *vol, u64 abs_lba, void *buf);
int ocire_fat_write_sect(ocire_fat_vol_t *vol, u64 abs_lba, const void *buf);

#endif /* OCIRE_FAT_H */
