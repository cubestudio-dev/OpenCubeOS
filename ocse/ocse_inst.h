/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_inst.h
 * Purpose: the installer engine - ocbl-install (write the OCBL MBR
 *   flavor: stage1 at LBA0 with the real partition table preserved +
 *   the core image in the gap before the first partition, patch
 *   block and all, verified by full read-back), the `install` state
 *   machine (medium check -> FAT32 target mounted -> OCOS.TAR
 *   extraction -> boot code -> boot flag -> completion notice) and
 *   the `config` viewer/editor for the installed /etc/opencube.conf.
 *
 * Boot-layout contract (matches ocbl/bios/stage1.asm):
 *   - stage1_mbr (512 B) lands at disk LBA 0; bytes 446..509 are
 *     copied over from the disk's CURRENT partition table so the
 *     edit history of mkpart/setflag survives the install;
 *   - the patch block at image offset 0x1A0 gets magic 'OCBL',
 *     core_lba = 1 (the first free sector), core_sectors;
 *   - the core image (OCBLMBR.BIN[512..]) lands at LBA 1..core_secs;
 *     the gap between LBA 1 and the first partition start must fit
 *     it (mkpart aligns partitions at LBA 2048 = 1 MiB, leaving
 *     2047 sectors - the core needs ~62).
 *   - OCBL reads FAT12/16/32 and ISO9660 only, so a BOOTABLE install
 *     target must be FAT32; `install` enforces that honestly.
 */
#ifndef OCSE_INST_H
#define OCSE_INST_H

#include "types.h"

/* Write the OCBL boot code onto disk `disk_idx`. `medium_path` is a
 * VFS path to OCBLMBR.BIN; `work` is a caller-provided scratch
 * buffer of at least 65536 bytes (the image + the verify read-back).
 * Prints progress lines. Returns 0, or negative:
 *   -1 medium unreadable/invalid, -2 disk has no partition table,
 *   -3 no room for the core before the first partition,
 *   -4 write failed, -5 read-back verify failed. */
int ocse_inst_ocbl(int disk_idx, const char* medium_path,
                   u8* work, int work_sz);

/* The `install <disk> <n> confirm` state machine. Returns 0 or the
 * (positive) count of the step that failed:
 *   1 medium, 2 target device, 3 target filesystem, 4 mount,
 *   5 confirm gate, 6 extraction, 7 boot code, 8 boot flag.
 * Prints per-step progress plus the completion notice. */
int ocse_inst_install(const char* disk, int part_no, int confirm,
                      u8* work, int work_sz);

/* `config` (no args): show the installed system's opencube.conf.
 * `config set <key> <value>`: validate + rewrite one key line.
 * Both auto-detect the mounted FAT32 install target. Returns a
 * command exit code (0 ok). */
int ocse_inst_config_show(void);
int ocse_inst_config_set(const char* key, const char* value);

#endif /* OCSE_INST_H */
