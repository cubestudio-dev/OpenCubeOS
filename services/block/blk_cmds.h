/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/block command surface.
 *
 * The v1 oc> block commands, migrated verbatim from recovery/
 * (shell_cmds_disk.c, shell_cmds_disk_test.c), core/main.c (status
 * commands) and drivers/block/driver_block_disk_setup.c (setup
 * commands).  Function bodies are unchanged - the screen-console
 * output is captured by core_kcap_run_fn() when invoked through
 * SYS_L1SVC.  Exported for services/block/block_l1.c (the op table).
 */
#ifndef OC_BLK_CMDS_H
#define OC_BLK_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* blk_cmds.c (from recovery/shell_cmds_disk.c) */
int blk_cmd_lsblk(const char *args);
int blk_cmd_parted(const char *args);
int blk_cmd_mkfs_fat32(const char *args);
int blk_cmd_mkfs_exfat(const char *args);
int blk_cmd_mkfs_ext4(const char *args);
int blk_cmd_fsck(const char *args);
int blk_cmd_sync(const char *args);

/* blk_cmds_test.c (from recovery/shell_cmds_disk_test.c) */
int blk_cmd_ahci_test(const char *args);
int blk_cmd_nvme_test(const char *args);
int blk_cmd_ata_dma_test(const char *args);
int blk_cmd_virtio_blk_test(const char *args);
int blk_cmd_disk_rw_test(const char *args);
int blk_cmd_partition_test(const char *args);
int blk_cmd_fs_mount_test(const char *args);
int blk_cmd_real_hw_test(const char *args);

/* blk_cmds_status.c (from core/main.c inline commands) */
int blk_cmd_ahci(const char *args);
int blk_cmd_nvme(const char *args);
int blk_cmd_ata(const char *args);
int blk_cmd_dskstat(const char *args);

/* blk_cmds_setup.c (from drivers/block/driver_block_disk_setup.c) */
int blk_cmd_abdisk(const char *args);
int blk_cmd_install(const char *args);
int blk_cmd_grubinstall(const char *args);
int blk_cmd_abcfg_show(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_BLK_CMDS_H */
