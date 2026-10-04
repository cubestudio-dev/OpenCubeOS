/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07 / WP-10a
 * File: kernel/disk_cmds.h
 * Purpose: WP-07 disk shell commands + WP-10a shared fs helpers.
 */
#ifndef OC_DISK_CMDS_H
#define OC_DISK_CMDS_H
void shell_cmds_disk_register(void);

/* WP-10a: format the block device `dev_idx` as FAT32 (the logic behind
 * the `mkfs.fat32` shell command), exported so the fs_mount_test command
 * exercises the exact same formatting code.  Returns 0 on success. */
int driver_block_mkfs_fat32_device(int dev_idx);
#endif
