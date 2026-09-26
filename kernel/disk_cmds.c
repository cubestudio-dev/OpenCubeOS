/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/disk_cmds.c
 * Purpose: WP-07 shell commands for disk management.
 *   lsblk, parted, mkfs.fat32, mkfs.exfat, mkfs.ext4, fsck, sync
 *   Plus enhanced df/du that use the real VFS.
 */
#include "disk_cmds.h"
#include "shell.h"
#include "blk.h"
#include "blk_cache.h"
#include "part.h"
#include "vfs.h"
#include "ramfs.h"
#include "fat32.h"
#include "console.h"
#include "string.h"
#include "heap.h"

/* ---- lsblk: list block devices ---- */
static int cmd_lsblk(const char *args) {
    (void)args;
    blk_list_devices();
    blk_cache_stats_t cs;
    blk_cache_get_stats(&cs);
    char line[120]; char n[24];
    oc_strcpy(line, "Disk cache: "); oc_u64_to_str(cs.used, n); oc_strcat(line, n);
    oc_strcat(line, "/64 slots  hits="); oc_u64_to_str(cs.hits, n); oc_strcat(line, n);
    oc_strcat(line, " misses="); oc_u64_to_str(cs.misses, n); oc_strcat(line, n);
    oc_strcat(line, "\n");
    oc_console_puts(line);
    return 0;
}

/* ---- parted: show partition table ---- */
static int cmd_parted(const char *args) {
    int dev_idx = 0;
    if (args[0]) {
        dev_idx = blk_find_device(args);
        if (dev_idx < 0) {
            oc_console_puts("parted: device not found\n");
            return 1;
        }
    } else {
        /* Use first device. */
        if (blk_num_devices() == 0) {
            oc_console_puts("no block devices\n");
            return 1;
        }
        dev_idx = 0;
        while (!blk_get_device(dev_idx) && dev_idx < 8) dev_idx++;
    }
    part_table_t tbl;
    if (part_parse(dev_idx, &tbl) != 0) {
        oc_console_puts("parted: no partition table found\n");
        return 1;
    }
    part_print(&tbl);
    return 0;
}

/* ---- mkfs.fat32: format a device as FAT32 ---- */
static int cmd_mkfs_fat32(const char *args) {
    if (!args[0]) {
        oc_console_puts("usage: mkfs.fat32 <device>\n");
        return 1;
    }
    int dev_idx = blk_find_device(args);
    if (dev_idx < 0) {
        oc_console_puts("mkfs.fat32: device not found\n");
        return 1;
    }
    /* Minimal FAT32 format: write a BPB + empty FAT + root dir cluster. */
    u8 buf[512];
    oc_memset(buf, 0, 512);
    /* BPB */
    buf[0] = 0xEB; buf[1] = 0x58; buf[2] = 0x90;   /* jmp */
    oc_memcpy(buf + 3, "MSWIN4.1", 8);              /* OEM */
    *(u16*)(buf + 11) = 512;                         /* bytes per sector */
    buf[13] = 1;                                     /* sectors per cluster */
    *(u16*)(buf + 14) = 32;                          /* reserved sectors */
    buf[16] = 2;                                     /* num FATs */
    *(u16*)(buf + 17) = 0;                           /* root entries (0 for FAT32) */
    *(u16*)(buf + 19) = 0;                           /* total sectors 16 */
    buf[21] = 0xF8;                                  /* media descriptor */
    *(u16*)(buf + 22) = 0;                           /* FAT size 16 (0 for FAT32) */
    *(u16*)(buf + 24) = 63;                          /* sectors per track */
    *(u16*)(buf + 26) = 255;                         /* heads */
    *(u32*)(buf + 28) = 0;                           /* hidden sectors */
    blk_device_t *dev = blk_get_device(dev_idx);
    *(u32*)(buf + 32) = (u32)dev->sectors;           /* total sectors 32 */
    *(u32*)(buf + 36) = 128;                         /* FAT size sectors */
    *(u16*)(buf + 40) = 0;                           /* ext flags */
    *(u16*)(buf + 42) = 0;                           /* FS version */
    *(u32*)(buf + 44) = 2;                           /* root cluster */
    *(u16*)(buf + 48) = 1;                           /* FS info */
    *(u16*)(buf + 50) = 6;                           /* backup boot sector */
    /* P1-16 FIX: offset 66 is the drive_number/reserved area, NOT a FAT entry.
     * The "end-of-chain marker for root" belongs in the FAT itself (cluster 2),
     * which we write below. Instead, set the fs_type string at offset 82
     * (required by our FAT32 driver's mount check). */
    buf[64] = 0x80;                                  /* drive number */
    buf[66] = 0x29;                                  /* extended boot signature */
    *(u32*)(buf + 67) = 0x12345678;                  /* volume serial number */
    oc_memcpy(buf + 71, "NO NAME    ", 11);          /* volume label */
    oc_memcpy(buf + 82, "FAT32   ", 8);              /* fs_type string */
    buf[510] = 0x55; buf[511] = 0xAA;
    blk_write_sectors_raw(dev_idx, 0, 1, buf);

    /* Write FAT entries: cluster 2 (root) = EOC, clusters 3+ = free. */
    u8 fat[512];
    oc_memset(fat, 0, 512);
    *(u32*)(fat + 0) = 0x0FFFFFF8;   /* cluster 0 */
    *(u32*)(fat + 4) = 0x0FFFFFF8;   /* cluster 1 */
    *(u32*)(fat + 8) = 0x0FFFFFFF;   /* cluster 2 = root dir, EOC */
    /* FAT starts at sector 32 (reserved), write to both FAT copies.
     * BUG-014 FIX: Zero-fill ALL FAT sectors (each FAT is 128 sectors).
     * Old code only wrote the first sector of each FAT copy (12 bytes),
     * leaving sectors 33..159 and 161..287 with stale data from the
     * previous filesystem. Now we write zeroed sectors for the full
     * FAT area (32..287). */
    {
        u8 zero_fat[512];
        oc_memset(zero_fat, 0, 512);
        /* Zero all reserved + FAT sectors (32 to 32+2*128-1 = 287) */
        for (int s = 32; s < 32 + 2 * 128; s++) {
            blk_write_sectors_raw(dev_idx, s, 1, zero_fat);
        }
        /* Now write the FAT entry markers */
        for (int i = 0; i < 2; i++)
            blk_write_sectors_raw(dev_idx, 32 + i * 128, 1, fat);
    }

    /* Root dir cluster (cluster 2) = data_start sector. Data starts at 32 + 2*128 = 288. */
    u8 dir[512];
    oc_memset(dir, 0, 512);
    blk_write_sectors_raw(dev_idx, 288, 1, dir);

    oc_console_puts("FAT32 formatted on ");
    oc_console_puts(args);
    oc_console_puts("\n");
    return 0;
}

static int cmd_mkfs_exfat(const char *args) {
    if (!args[0]) { oc_console_puts("usage: mkfs.exfat <device>\n"); return 1; }
    int dev_idx = blk_find_device(args);
    if (dev_idx < 0) { oc_console_puts("mkfs.exfat: device not found\n"); return 1; }
    blk_device_t *dev = blk_get_device(dev_idx);
    if (!dev) return 1;
    /* P2-74: Minimal exFAT format. */
    u8 buf[512];
    oc_memset(buf, 0, 512);
    /* Boot sector */
    buf[0] = 0xEB; buf[1] = 0x76; buf[2] = 0x90;
    oc_memcpy(buf + 3, "EXFAT   ", 8);
    /* Use fixed layout: FAT at sector 32, heap at 64, root dir at cluster 2 */
    u64 total_sectors = dev->sectors;
    *(u64*)(buf + 64) = 0;                    /* partition_offset */
    *(u64*)(buf + 72) = total_sectors;        /* volume_length */
    *(u32*)(buf + 80) = 32;                   /* fat_offset */
    *(u32*)(buf + 84) = 1;                    /* fat_length */
    *(u32*)(buf + 88) = 64;                   /* cluster_heap_offset */
    u32 cluster_count = (u32)(total_sectors - 64);
    *(u32*)(buf + 92) = cluster_count;        /* cluster_count */
    *(u32*)(buf + 96) = 2;                    /* first_cluster_of_root_directory */
    *(u16*)(buf + 100) = 0x0100;              /* fs_revision */
    buf[108] = 9;                              /* bytes_per_sector_shift (512) */
    buf[109] = 0;                              /* sectors_per_cluster_shift (1) */
    buf[110] = 1;                              /* number_of_fats */
    buf[510] = 0x55; buf[511] = 0xAA;
    blk_write_sectors_raw(dev_idx, 0, 1, buf);
    /* FAT: cluster 2 = root dir, EOC */
    u8 fat[512];
    oc_memset(fat, 0, 512);
    *(u32*)(fat + 0) = 0xFFFFFFF8;
    *(u32*)(fat + 4) = 0xFFFFFFFF;
    *(u32*)(fat + 8) = 0xFFFFFFFF;  /* cluster 2 = EOC */
    blk_write_sectors_raw(dev_idx, 32, 1, fat);
    /* Root directory at cluster 2 = sector 64 */
    u8 rootdir[512];
    oc_memset(rootdir, 0, 512);
    /* Volume label entry */
    rootdir[0] = 0x83;
    oc_memcpy(rootdir + 1, "OCOS       ", 11);
    blk_write_sectors_raw(dev_idx, 64, 1, rootdir);
    oc_console_puts("exFAT formatted on ");
    oc_console_puts(args);
    oc_console_puts("\n");
    return 0;
}

static int cmd_mkfs_ext4(const char *args) {
    if (!args[0]) { oc_console_puts("usage: mkfs.ext4 <device>\n"); return 1; }
    int dev_idx = blk_find_device(args);
    if (dev_idx < 0) { oc_console_puts("mkfs.ext4: device not found\n"); return 1; }
    blk_device_t *dev = blk_get_device(dev_idx);
    if (!dev) return 1;
    /* P2-75: Minimal ext4 format — write superblock at byte offset 1024. */
    u8 buf[512];
    oc_memset(buf, 0, 512);
    /* Superblock at sector 2 (byte 1024) */
    *(u16*)(buf + 56) = 0xEF53;               /* magic */
    *(u32*)(buf + 0) = 0;                     /* s_inodes_count (placeholder) */
    *(u32*)(buf + 4) = (u32)dev->sectors;     /* s_blocks_count_lo */
    *(u32*)(buf + 24) = 0;                    /* s_log_block_size (1024 bytes) */
    *(u32*)(buf + 32) = 8192;                 /* s_blocks_per_group */
    *(u32*)(buf + 40) = 16384;               /* s_inodes_per_group */
    *(u16*)(buf + 88) = 256;                 /* s_inode_size */
    blk_write_sectors_raw(dev_idx, 2, 1, buf);
    oc_console_puts("ext4 formatted on ");
    oc_console_puts(args);
    oc_console_puts(" (minimal — use mkfs.ext4 on host for full format)\n");
    return 0;
}

static int cmd_fsck(const char *args) {
    /* BUG-022 FIX: fsck now returns non-zero (error) instead of 0 (success).
     * Old code was a stub that printed 'not implemented' but returned 0,
     * causing scripts to treat fsck as successful. Now it clearly fails. */
    (void)args;
    oc_console_puts("fsck: filesystem check not yet implemented\n");
    return 1;  /* non-zero = failure */
}

/* ---- sync: flush disk cache ---- */
static int cmd_sync(const char *args) {
    (void)args;
    blk_cache_flush();
    oc_console_puts("disk cache flushed\n");
    return 0;
}

void disk_cmds_register(void) {
    /* P1-18 FIX: df/du/mount/umount are registered in file_cmds.c (WP-05).
     * Only register WP-07-specific commands here to avoid duplicates. */
    shell_register_command("lsblk",       cmd_lsblk,       "list block devices");
    shell_register_command("parted",      cmd_parted,      "show partition table (parted [dev])");
    shell_register_command("mkfs.fat32",  cmd_mkfs_fat32,  "format FAT32 (mkfs.fat32 <dev>)");
    shell_register_command("mkfs.exfat",  cmd_mkfs_exfat,  "format exFAT");
    shell_register_command("mkfs.ext4",   cmd_mkfs_ext4,   "format ext4");
    shell_register_command("fsck",        cmd_fsck,        "filesystem check");
    shell_register_command("sync",        cmd_sync,        "flush disk cache");
}
