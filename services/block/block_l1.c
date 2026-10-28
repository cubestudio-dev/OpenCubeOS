/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/block L1 surface.
 *
 * SYS_L1SVC entry for the block service: one op per migrated v1
 * command (see services/block/blk_cmds*.c).  The command bodies are
 * the v1 code, executed with their screen-console output captured
 * into the caller's buffer (shell_run_fn_captured) - ring-3 ush owns
 * presentation, the service owns behaviour.
 */
#include "block_svc.h"
#include "drv_internal.h"
#include "core_registry.h"
#include "l1svc_defs.h"
#include "blk_cmds.h"
#include "core_kcap.h"

typedef int (*blk_cmd_fn_t)(const char *);

static int block_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    blk_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_BLK_AHCI:          fn = blk_cmd_ahci; break;
        case L1SVC_BLK_NVME:          fn = blk_cmd_nvme; break;
        case L1SVC_BLK_ATA:           fn = blk_cmd_ata; break;
        case L1SVC_BLK_DSKSTAT:       fn = blk_cmd_dskstat; break;
        case L1SVC_BLK_LSBLK:         fn = blk_cmd_lsblk; break;
        case L1SVC_BLK_PARTED:        fn = blk_cmd_parted; break;
        case L1SVC_BLK_MKFS_FAT32:    fn = blk_cmd_mkfs_fat32; break;
        case L1SVC_BLK_MKFS_EXFAT:    fn = blk_cmd_mkfs_exfat; break;
        case L1SVC_BLK_MKFS_EXT4:     fn = blk_cmd_mkfs_ext4; break;
        case L1SVC_BLK_FSCK:          fn = blk_cmd_fsck; break;
        case L1SVC_BLK_SYNC:          fn = blk_cmd_sync; break;
        case L1SVC_BLK_AHCI_TEST:     fn = blk_cmd_ahci_test; break;
        case L1SVC_BLK_NVME_TEST:     fn = blk_cmd_nvme_test; break;
        case L1SVC_BLK_ATA_DMA_TEST:  fn = blk_cmd_ata_dma_test; break;
        case L1SVC_BLK_VIRTIO_TEST:   fn = blk_cmd_virtio_blk_test; break;
        case L1SVC_BLK_RW_TEST:       fn = blk_cmd_disk_rw_test; break;
        case L1SVC_BLK_PART_TEST:     fn = blk_cmd_partition_test; break;
        case L1SVC_BLK_FS_MOUNT_TEST: fn = blk_cmd_fs_mount_test; break;
        case L1SVC_BLK_REAL_HW_TEST:  fn = blk_cmd_real_hw_test; break;
        case L1SVC_BLK_ABDISK:        fn = blk_cmd_abdisk; break;
        case L1SVC_BLK_INSTALL:       fn = blk_cmd_install; break;
        case L1SVC_BLK_GRUB_INSTALL:  fn = blk_cmd_grubinstall; break;
        case L1SVC_BLK_ABCFG:         fn = blk_cmd_abcfg_show; break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: the block service identity now carries its ring-3 surface.
 * (init/late unchanged from the group D declaration.) */
OC_SERVICE_DECL_L1("block", "block service (device table + RW/flush dispatch + cache gate + ring-3 surface)",
                   block_svc_init, (oc_service_late_fn_t)0,
                   block_l1_dispatch);
