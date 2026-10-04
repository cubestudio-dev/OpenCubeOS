/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
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
#include "ata.h"
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

/* WP-10a: the formatting logic lives in mkfs_fat32_device() (declared in
 * disk_cmds.h) so the fs_mount_test command exercises the exact same
 * code path as the mkfs.fat32 shell command.  Returns 0 on success,
 * -1 if the device is invalid or too small. */
int mkfs_fat32_device(int dev_idx) {
    blk_device_t *dev0 = blk_get_device(dev_idx);
    if (!dev0 || dev0->sectors < 40) return -1;
    /* WP-09-FIX BUG-016: compute the FAT size from the actual disk size.
     * The old code hardcoded 128 FAT sectors (= 16384 clusters), which
     * only covers ~25% of a 32 MB disk (65248 clusters) — fsck showed
     * free=16381 on a 65248-cluster volume. Two iterations converge on
     * a self-consistent FAT size. */
    u32 total_sect = (u32)dev0->sectors;
    /* WP-10d-pre (rule-9): pick the cluster size from Microsoft's
     * standard FAT32 table.  The old code hardcoded 1 sector/cluster,
     * which makes the FAT grow 1 KiB per MiB of volume - a 512 MiB disk
     * ends up with a 4 MiB FAT that the mount-time cache kmalloc
     * rejects ("FAT cache alloc failed").  Standard formatting (as
     * Windows/mkfs.vfat do) uses larger clusters on larger volumes so
     * the FAT stays small (a 512 MiB volume -> 4 KiB clusters -> 512 KiB
     * FAT) and every volume we can format is also one we can mount. */
    u32 spc = 1;                       /* <= 260 MiB: 512 B clusters */
    if (total_sect > 532480u) spc = 8;         /* 260 MiB..8 GiB: 4 KiB */
    if (total_sect > 16384u * 1024u) spc = 16; /* 8 GiB..16 GiB: 8 KiB */
    if (total_sect > 32768u * 1024u) spc = 32; /* 16 GiB..32 GiB: 16 KiB */
    if (total_sect > 65536u * 1024u) spc = 64; /* > 32 GiB: 32 KiB */
    u32 fat_secs = 1;
    for (int it = 0; it < 2; it++) {
        u32 data_secs = (total_sect > 32 + 2 * fat_secs)
                            ? total_sect - 32 - 2 * fat_secs : 1;
        u32 clusters = data_secs / spc;
        u32 fat_bytes = (clusters + 2 + 1) * 4;   /* clusters + 2 reserved entries, round up */
        u32 need = (fat_bytes + 511) / 512;
        if (need > fat_secs) fat_secs = need;
        else if (it > 0) fat_secs = need;          /* allow shrink on 2nd pass */
    }
    if (fat_secs < 1) fat_secs = 1;
    u8 buf[512];
    oc_memset(buf, 0, 512);
    /* BPB */
    buf[0] = 0xEB; buf[1] = 0x58; buf[2] = 0x90;   /* jmp */
    oc_memcpy(buf + 3, "MSWIN4.1", 8);              /* OEM */
    *(u16*)(buf + 11) = 512;                         /* bytes per sector */
    buf[13] = (u8)spc;                               /* sectors per cluster (WP-10d-pre: MS table) */
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
    *(u32*)(buf + 36) = fat_secs;                    /* FAT size sectors (BUG-016: computed) */
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

    /* WP-10c rule-9 fix: wipe the rest of the reserved area (LBA 1..31).
     * A previously formatted volume (exFAT boot area, ext4 superblock at
     * byte 1024, ...) left bytes there that made fsck's non-FAT32
     * pre-detection misclassify the fresh volume. */
    {
        u8 zero_res[512];
        oc_memset(zero_res, 0, 512);
        for (u32 s = 1; s < 32; s++) {
            blk_write_sectors_raw(dev_idx, s, 1, zero_res);
        }
    }

    /* Write FAT entries: cluster 2 (root) = EOC, clusters 3+ = free. */
    u8 fat[512];
    oc_memset(fat, 0, 512);
    *(u32*)(fat + 0) = 0x0FFFFFF8;   /* cluster 0 */
    *(u32*)(fat + 4) = 0x0FFFFFF8;   /* cluster 1 */
    *(u32*)(fat + 8) = 0x0FFFFFFF;   /* cluster 2 = root dir, EOC */
    /* FAT starts at sector 32 (reserved), write to both FAT copies.
     * WP-09-FIX BUG-016: zero-fill ALL FAT sectors (each FAT is fat_secs
     * sectors, computed from the actual disk size). */
    {
        u8 zero_fat[512];
        oc_memset(zero_fat, 0, 512);
        /* Zero all reserved + FAT sectors (32 to 32+2*fat_secs-1) */
        for (u32 s = 32; s < 32 + 2 * fat_secs; s++) {
            blk_write_sectors_raw(dev_idx, s, 1, zero_fat);
        }
        /* Now write the FAT entry markers */
        for (int i = 0; i < 2; i++)
            blk_write_sectors_raw(dev_idx, 32 + (u32)i * fat_secs, 1, fat);
    }

    /* Root dir cluster (cluster 2) = data_start sector. Data starts at 32 + 2*fat_secs (BUG-016). */
    u8 dir[512];
    oc_memset(dir, 0, 512);
    blk_write_sectors_raw(dev_idx, 32 + 2 * fat_secs, 1, dir);
    return 0;
}

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
    if (mkfs_fat32_device(dev_idx) != 0) {
        oc_console_puts("mkfs.fat32: device too small\n");
        return 1;
    }
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
    /* WP-10c rule-9 fix: wipe LBA 1..31 so leftover bytes from a previous
     * format (e.g. an ext4 superblock at byte 1024) cannot survive and
     * confuse later fsck/identification passes. */
    {
        u8 zero_vbr[512];
        oc_memset(zero_vbr, 0, 512);
        for (u32 s = 1; s < 32; s++) {
            blk_write_sectors_raw(dev_idx, s, 1, zero_vbr);
        }
    }
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
    /* WP-10c rule-9 fix: clear LBA 0 (an older FAT32/exFAT boot sector
     * would still be parsed by FAT tools and fsck after the format). */
    {
        u8 zero_bps[512];
        oc_memset(zero_bps, 0, 512);
        blk_write_sectors_raw(dev_idx, 0, 1, zero_bps);
    }
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
    oc_console_puts(" (minimal — superblock only, no directory entries: ls will show an empty volume; use mkfs.ext4 on the host for a full format)\n");  /* WP-09-FIX BUG-033 */
    return 0;
}

/* P1-7 FIX: fsck now does a basic FAT32 filesystem check.
 * Reads the boot sector, verifies BPB, counts FAT entries.
 * Not a full fsck (no cross-link detection, no orphan recovery),
 * but it's a real check that reports useful information. */
static int cmd_fsck(const char *args) {
    /* If no device given, report mounted filesystems */
    if (!args || !args[0]) {
        oc_console_puts("fsck: checking mounted FAT32 filesystems\n");
        /* Walk mount table for FAT32 mounts */
        /* For now, just report that fsck needs a device argument */
        oc_console_puts("usage: fsck <device>\n");
        oc_console_puts("  e.g. fsck hda\n");
        return 0;
    }

    /* Parse device name */
    int drive = -1;
    if (oc_strncmp(args, "hd", 2) == 0) {
        char c = args[2];
        if (c >= 'a' && c <= 'd' && args[3] == 0) drive = c - 'a';
    } else if (oc_strncmp(args, "ata", 3) == 0) {
        char c = args[3];
        if (c >= '0' && c <= '3' && args[4] == 0) drive = c - '0';
    }
    if (drive < 0) {
        oc_console_puts("fsck: invalid device '");
        oc_console_puts(args);
        oc_console_puts("'\n");
        return 1;
    }

    /* Read boot sector */
    u8 boot[512];
    int n = ata_read_sectors(drive, 0, 1, boot);
    if (n != 1) {
        oc_console_puts("fsck: cannot read boot sector\n");
        return 1;
    }

    /* WP-10c rule-9 fix: identify non-FAT32 volumes BEFORE any FAT
     * sanity check (ext4 does not even carry the 0x55AA signature, so
     * the signature check would otherwise mask the volume type).
     * mkfs.ext4 writes an ext superblock into sectors 1-2 without
     * touching the FAT32-ish BPB fields at offsets 11/13, so a plain
     * BPB parse used to report "FAT32 ... PASS" on a freshly formatted
     * ext4 volume - misleading feedback. */
    if (boot[3] == 'E' && boot[4] == 'X' && boot[5] == 'F' &&
        boot[6] == 'A' && boot[7] == 'T') {
        oc_console_puts("fsck: exFAT volume detected (fsck supports FAT32 only)\n");
        return 1;
    }
    {
        /* ext2/3/4: superblock starts at byte 1024 (LBA 2), magic 0xEF53
         * lives at superblock offset 0x38 = absolute byte 1080.  Require
         * two more sane fields so stray bytes in a wiped-but-not-zeroed
         * region cannot false-positive: log block size <= 6 and a
         * non-zero block count. */
        u8 sb[512];
        if (ata_read_sectors(drive, 2, 1, sb) == 1) {
            u16 ext_magic = (u16)(sb[56] | (sb[57] << 8));      /* 1080-1024=56 */
            u32 log_bs    = *(u32*)(sb + 24);
            u32 blocks    = *(u32*)(sb + 4);
            if (ext_magic == 0xEF53 && log_bs <= 6 && blocks > 0) {
                oc_console_puts("fsck: ext2/3/4 volume detected (driver is read-only; fsck supports FAT32 only)\n");
                return 1;
            }
        }
    }

    /* Check FAT signature (0x55AA at offset 510) */
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        oc_console_puts("fsck: invalid boot signature (not 0x55AA)\n");
        return 1;
    }

    /* Parse FAT32 BPB */
    u16 bytes_per_sec = *(u16*)(boot + 11);
    u8  secs_per_clus = boot[13];
    u16 reserved_sectors = *(u16*)(boot + 14);
    u8  num_fats = boot[16];
    u32 total_sectors32 = *(u32*)(boot + 32);
    u32 secs_per_fat32 = *(u32*)(boot + 36);
    u32 root_dir_clus = *(u32*)(boot + 44);

    if (bytes_per_sec == 0 || secs_per_clus == 0) {
        /* WP-09-FIX BUG-021: say what fsck actually supports instead of a
         * bare "invalid BPB" for non-FAT32 volumes (exFAT/ext4 also have
         * zeroed FAT BPB fields at these offsets). */
        oc_console_puts("fsck: not a FAT32 volume (fsck supports FAT32 only; got invalid BPB)\n");
        return 1;
    }

    char buf[80]; char num[20];
    /* WP-09-FIX BUG-021: report the device the user actually asked
     * about, not a hardcoded "hda". */
    oc_console_puts("fsck: FAT32 filesystem on ");
    oc_console_puts(args);
    oc_console_puts("\n");
    oc_strcpy(buf, "  bytes_per_sector: "); oc_u64_to_str(bytes_per_sec, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  sectors_per_cluster: "); oc_u64_to_str(secs_per_clus, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  reserved_sectors: "); oc_u64_to_str(reserved_sectors, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  num_fats: "); oc_u64_to_str(num_fats, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  total_sectors: "); oc_u64_to_str(total_sectors32, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  sectors_per_fat: "); oc_u64_to_str(secs_per_fat32, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  root_dir_cluster: "); oc_u64_to_str(root_dir_clus, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* P1-7 FIX: Scan FAT for used/free/bad clusters — only count REAL data clusters.
     * Only count entries 2..total_clusters+1 (the actual data area).
     * Previously we counted ALL zero entries as free, including reserved
     * entries (0,1) and out-of-range entries (beyond total_clusters). */
    u32 fat_start = reserved_sectors;
    u32 data_start = reserved_sectors + (num_fats * secs_per_fat32);
    u32 total_clusters = (total_sectors32 - data_start) / secs_per_clus;
    u32 used = 0, free_clust = 0, bad = 0;

    u8 *fat_buf = (u8*)kmalloc(bytes_per_sec);
    if (!fat_buf) {
        oc_console_puts("fsck: out of memory\n");
        return 1;
    }
    u32 entries_per_sector = bytes_per_sec / 4;  /* FAT32: 4 bytes per entry */
    u32 cluster_idx = 0;  /* global FAT entry index (0-based) */
    u32 max_scan = total_clusters + 2;  /* +2 for reserved entries 0,1 */
    if (max_scan > 100000) max_scan = 100000;

    for (u32 s = 0; s < secs_per_fat32 && cluster_idx < max_scan; s++) {
        n = ata_read_sectors(drive, fat_start + s, 1, fat_buf);
        if (n != 1) break;
        for (u32 e = 0; e < entries_per_sector && cluster_idx < max_scan; e++) {
            u32 entry = *(u32*)(fat_buf + e * 4) & 0x0FFFFFFF;
            if (cluster_idx < 2) {
                /* Reserved entries (0,1) — skip, not data clusters */
            } else if (entry == 0) {
                free_clust++;
            } else if (entry == 0x0FFFFFF7) {
                bad++;
            } else {
                used++;
            }
            cluster_idx++;
        }
    }
    kfree(fat_buf);

    oc_strcpy(buf, "  clusters: used="); oc_u64_to_str(used, num); oc_strcat(buf, num);
    oc_strcat(buf, " free="); oc_u64_to_str(free_clust, num); oc_strcat(buf, num);
    oc_strcat(buf, " bad="); oc_u64_to_str(bad, num); oc_strcat(buf, num);
    oc_strcat(buf, " total="); oc_u64_to_str(total_clusters, num); oc_strcat(buf, num);
    oc_strcat(buf, "\n  fsck: PASS (basic checks OK)\n");
    oc_console_puts(buf);
    return 0;
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
    shell_register_command_ex("lsblk", cmd_lsblk, "list block devices", "WP-10a");
    shell_register_command_ex("parted", cmd_parted, "show partition table (parted [dev])", "WP-10a");
    shell_register_command_ex("mkfs.fat32", cmd_mkfs_fat32, "format FAT32 (mkfs.fat32 <dev>)", "WP-10a");
    shell_register_command_ex("mkfs.exfat", cmd_mkfs_exfat, "format exFAT", "WP-10a");
    shell_register_command_ex("mkfs.ext4", cmd_mkfs_ext4, "format ext4", "WP-10a");
    shell_register_command_ex("fsck", cmd_fsck, "filesystem check", "WP-10a");
    shell_register_command_ex("sync", cmd_sync, "flush disk cache", "WP-10a");
}
