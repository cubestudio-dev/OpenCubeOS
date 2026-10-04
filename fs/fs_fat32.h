/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/fat32.h
 * Purpose: FAT32 file system driver (read/write) — sits on top of ATA.
 *
 * Register with VFS via fs_fat32_init(). Mount a partition via fs_fat32_mount().
 * The implementation caches the first FAT in memory and walks cluster chains
 * on demand. WP-07 adds write support: file creation, cluster allocation,
 * chain extension, directory entry updates, file deletion.
 */
#ifndef OC_FAT32_H
#define OC_FAT32_H

#include "types.h"

/* Register the "fat32" fs type with VFS. Does NOT mount anything. */
void fs_fat32_init(void);

/* Convenience: mount the FAT32 file system on `device` (e.g. "ata0") at the
 * given mount point. Returns 0 on success or a negative value on error.
 * Equivalent to fs_vfs_mount("fat32", mount_point, device). */
int fs_fat32_mount(const char *device, const char *mount_point);

/* Query mounted-FS stats. If no FAT32 is mounted, all outputs are 0. */
void fs_fat32_get_stats(u64 *total_sectors, u64 *free_clusters, u32 *cluster_size);

#endif /* OC_FAT32_H */
