/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/disk_cmds.c
 * Purpose: WP-07 shell commands for disk management.
 *   lsblk, parted, mkfs.fat32, mkfs.exfat, mkfs.ext4, fsck, sync
 *   Plus enhanced df/du that use the real VFS.
 */
#include "shell_cmds_disk.h"
#include "shell.h"
#include "driver_block_blk.h"
#include "driver_block_cache.h"
#include "driver_block_part.h"
#include "fs_vfs.h"
#include "fs_ramfs.h"
#include "fs_fat32.h"
#include "driver_block_ata.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_heap.h"

/* ---- lsblk: list block devices ---- */
static int shell_cmd_lsblk(const char *args) {
    (void)args;
    driver_block_list_devices();
    driver_block_cache_stats_t cs;
    driver_block_cache_get_stats(&cs);
    char line[120]; char n[24];
    strcpy(line, "Disk cache: "); u64_to_str(cs.used, n); strcat(line, n);
    strcat(line, "/64 slots  hits="); u64_to_str(cs.hits, n); strcat(line, n);
    strcat(line, " misses="); u64_to_str(cs.misses, n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
    return 0;
}

/* ---- parted: show partition table ---- */
static int shell_cmd_parted(const char *args) {
    int dev_idx = 0;
    if (args[0]) {
        dev_idx = driver_block_find_device(args);
        if (dev_idx < 0) {
            screen_console_puts("parted: device not found\n");
            return 1;
        }
    } else {
        /* Use first device. */
        if (driver_block_num_devices() == 0) {
            screen_console_puts("no block devices\n");
            return 1;
        }
        dev_idx = 0;
        while (!driver_block_get_device(dev_idx) && dev_idx < 8) dev_idx++;
    }
    driver_block_part_table_t tbl;
    if (driver_block_part_parse(dev_idx, &tbl) != 0) {
        screen_console_puts("parted: no partition table found\n");
        return 1;
    }
    driver_block_part_print(&tbl);
    return 0;
}

/* ---- mkfs.fat32: format a device as FAT32 ---- */

/* WP-10a: the formatting logic lives in driver_block_mkfs_fat32_device() (declared in
 * disk_cmds.h) so the fs_mount_test command exercises the exact same
 * code path as the mkfs.fat32 shell command.  Returns 0 on success,
 * -1 if the device is invalid or too small. */
int driver_block_mkfs_fat32_device(int dev_idx) {
    driver_block_device_t *dev0 = driver_block_get_device(dev_idx);
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
    memset(buf, 0, 512);
    /* BPB */
    buf[0] = 0xEB; buf[1] = 0x58; buf[2] = 0x90;   /* jmp */
    memcpy(buf + 3, "MSWIN4.1", 8);              /* OEM */
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
    driver_block_device_t *dev = driver_block_get_device(dev_idx);
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
    memcpy(buf + 71, "NO NAME    ", 11);          /* volume label */
    memcpy(buf + 82, "FAT32   ", 8);              /* fs_type string */
    buf[510] = 0x55; buf[511] = 0xAA;
    driver_block_write_sectors_raw(dev_idx, 0, 1, buf);

    /* WP-10c rule-9 fix: wipe the rest of the reserved area (LBA 1..31).
     * A previously formatted volume (exFAT boot area, ext4 superblock at
     * byte 1024, ...) left bytes there that made fsck's non-FAT32
     * pre-detection misclassify the fresh volume. */
    {
        u8 zero_res[512];
        memset(zero_res, 0, 512);
        for (u32 s = 1; s < 32; s++) {
            driver_block_write_sectors_raw(dev_idx, s, 1, zero_res);
        }
    }

    /* Write FAT entries: cluster 2 (root) = EOC, clusters 3+ = free. */
    u8 fat[512];
    memset(fat, 0, 512);
    *(u32*)(fat + 0) = 0x0FFFFFF8;   /* cluster 0 */
    *(u32*)(fat + 4) = 0x0FFFFFF8;   /* cluster 1 */
    *(u32*)(fat + 8) = 0x0FFFFFFF;   /* cluster 2 = root dir, EOC */
    /* FAT starts at sector 32 (reserved), write to both FAT copies.
     * WP-09-FIX BUG-016: zero-fill ALL FAT sectors (each FAT is fat_secs
     * sectors, computed from the actual disk size). */
    {
        u8 zero_fat[512];
        memset(zero_fat, 0, 512);
        /* Zero all reserved + FAT sectors (32 to 32+2*fat_secs-1) */
        for (u32 s = 32; s < 32 + 2 * fat_secs; s++) {
            driver_block_write_sectors_raw(dev_idx, s, 1, zero_fat);
        }
        /* Now write the FAT entry markers */
        for (int i = 0; i < 2; i++)
            driver_block_write_sectors_raw(dev_idx, 32 + (u32)i * fat_secs, 1, fat);
    }

    /* Root dir cluster (cluster 2) = data_start sector. Data starts at 32 + 2*fat_secs (BUG-016). */
    u8 dir[512];
    memset(dir, 0, 512);
    driver_block_write_sectors_raw(dev_idx, 32 + 2 * fat_secs, 1, dir);
    return 0;
}

/* ---- BUG-0239 FIX (A15-13): mkfs.* confirmation gate ----
 * Formatting DESTROYS ALL DATA on the target device, and a typo'd device
 * name (hda vs hdb) used to wipe it on Enter with no way back. All three
 * mkfs.* commands now require the literal token `confirm` as the LAST
 * argument:
 *   mkfs.fat32 hda confirm
 * Without it NOTHING is written to the device - the command only prints
 * the data-loss warning and the usage line. Returns 0 when the caller may
 * proceed (device token copied into devbuf), 1 when refused (device
 * untouched). */
static int shell_mkfs_gate(const char *cmd, const char *args,
                           char *devbuf, int devbuf_len) {
    /* Extract the device token (first word). */
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && i < devbuf_len - 1) {
        devbuf[i] = args[i];
        i++;
    }
    if (args[i] && args[i] != ' ' && args[i] != '\t') {
        /* Device token did not fit - refuse, never guess. */
        screen_console_puts(cmd);
        screen_console_puts(": device name too long\n");
        return 1;
    }
    devbuf[i] = 0;
    if (!devbuf[0]) {
        screen_console_puts(cmd);
        screen_console_puts(": WARNING: formatting DESTROYS ALL DATA on the target device!\n");
        screen_console_puts("  usage: ");
        screen_console_puts(cmd);
        screen_console_puts(" <device> confirm\n");
        return 1;
    }
    /* Skip whitespace, then require the remainder to be exactly the
     * literal token `confirm` (trailing whitespace tolerated). Anything
     * else refuses and changes nothing. */
    while (args[i] == ' ' || args[i] == '\t') i++;
    char tail[16];
    int ti = 0;
    while (args[i] && ti < (int)sizeof(tail) - 1) tail[ti++] = args[i++];
    tail[ti] = 0;
    while (ti > 0 && (tail[ti - 1] == ' ' || tail[ti - 1] == '\t')) tail[--ti] = 0;
    if (args[i] || strcmp(tail, "confirm") != 0) {
        screen_console_puts(cmd);
        screen_console_puts(": WARNING: formatting DESTROYS ALL DATA on '");
        screen_console_puts(devbuf);
        screen_console_puts("'! Nothing was written.\n");
        screen_console_puts("  To really format, append the literal token 'confirm' as the last argument:\n");
        screen_console_puts("  usage: ");
        screen_console_puts(cmd);
        screen_console_puts(" <device> confirm\n");
        return 1;
    }
    return 0;
}

static int shell_cmd_mkfs_fat32(const char *args) {
    /* BUG-0239 FIX (A15-13): require the explicit `confirm` token. */
    char dev[BLK_DEV_NAME_LEN];
    if (shell_mkfs_gate("mkfs.fat32", args, dev, (int)sizeof(dev)) != 0) {
        return 1;
    }
    int dev_idx = driver_block_find_device(dev);
    if (dev_idx < 0) {
        screen_console_puts("mkfs.fat32: device not found\n");
        return 1;
    }
    if (driver_block_mkfs_fat32_device(dev_idx) != 0) {
        screen_console_puts("mkfs.fat32: device too small\n");
        return 1;
    }
    screen_console_puts("FAT32 formatted on ");
    screen_console_puts(dev);
    screen_console_puts("\n");
    return 0;
}

static int shell_cmd_mkfs_exfat(const char *args) {
    /* BUG-0239 FIX (A15-13): require the explicit `confirm` token. */
    char dev[BLK_DEV_NAME_LEN];
    if (shell_mkfs_gate("mkfs.exfat", args, dev, (int)sizeof(dev)) != 0) {
        return 1;
    }
    int dev_idx = driver_block_find_device(dev);
    if (dev_idx < 0) { screen_console_puts("mkfs.exfat: device not found\n"); return 1; }
    driver_block_device_t *devp = driver_block_get_device(dev_idx);
    if (!devp) return 1;
    /* P2-74: Minimal exFAT format. */
    u8 buf[512];
    memset(buf, 0, 512);
    /* Boot sector */
    buf[0] = 0xEB; buf[1] = 0x76; buf[2] = 0x90;
    memcpy(buf + 3, "EXFAT   ", 8);
    /* Use fixed layout: FAT at sector 32, heap at 64, root dir at cluster 2 */
    u64 total_sectors = devp->sectors;
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
    driver_block_write_sectors_raw(dev_idx, 0, 1, buf);
    /* WP-10c rule-9 fix: wipe LBA 1..31 so leftover bytes from a previous
     * format (e.g. an ext4 superblock at byte 1024) cannot survive and
     * confuse later fsck/identification passes. */
    {
        u8 zero_vbr[512];
        memset(zero_vbr, 0, 512);
        for (u32 s = 1; s < 32; s++) {
            driver_block_write_sectors_raw(dev_idx, s, 1, zero_vbr);
        }
    }
    /* FAT: cluster 2 = root dir, EOC */
    u8 fat[512];
    memset(fat, 0, 512);
    *(u32*)(fat + 0) = 0xFFFFFFF8;
    *(u32*)(fat + 4) = 0xFFFFFFFF;
    *(u32*)(fat + 8) = 0xFFFFFFFF;  /* cluster 2 = EOC */
    driver_block_write_sectors_raw(dev_idx, 32, 1, fat);
    /* Root directory at cluster 2 = sector 64 */
    u8 rootdir[512];
    memset(rootdir, 0, 512);
    /* Volume label entry */
    rootdir[0] = 0x83;
    memcpy(rootdir + 1, "OCOS       ", 11);
    driver_block_write_sectors_raw(dev_idx, 64, 1, rootdir);
    screen_console_puts("exFAT formatted on ");
    screen_console_puts(dev);
    screen_console_puts("\n");
    return 0;
}

static int shell_cmd_mkfs_ext4(const char *args) {
    /* BUG-0239 FIX (A15-13): require the explicit `confirm` token. */
    char dev[BLK_DEV_NAME_LEN];
    if (shell_mkfs_gate("mkfs.ext4", args, dev, (int)sizeof(dev)) != 0) {
        return 1;
    }
    int dev_idx = driver_block_find_device(dev);
    if (dev_idx < 0) { screen_console_puts("mkfs.ext4: device not found\n"); return 1; }
    driver_block_device_t *devp = driver_block_get_device(dev_idx);
    if (!devp) return 1;
    /* P2-75: Minimal ext4 format — write superblock at byte offset 1024. */
    u8 buf[512];
    memset(buf, 0, 512);
    /* WP-10c rule-9 fix: clear LBA 0 (an older FAT32/exFAT boot sector
     * would still be parsed by FAT tools and fsck after the format). */
    {
        u8 zero_bps[512];
        memset(zero_bps, 0, 512);
        driver_block_write_sectors_raw(dev_idx, 0, 1, zero_bps);
    }
    /* Superblock at sector 2 (byte 1024) */
    *(u16*)(buf + 56) = 0xEF53;               /* magic */
    *(u32*)(buf + 0) = 0;                     /* s_inodes_count (placeholder) */
    *(u32*)(buf + 4) = (u32)devp->sectors;    /* s_blocks_count_lo */
    *(u32*)(buf + 24) = 0;                    /* s_log_block_size (1024 bytes) */
    *(u32*)(buf + 32) = 8192;                 /* s_blocks_per_group */
    *(u32*)(buf + 40) = 16384;               /* s_inodes_per_group */
    *(u16*)(buf + 88) = 256;                 /* s_inode_size */
    driver_block_write_sectors_raw(dev_idx, 2, 1, buf);
    screen_console_puts("ext4 formatted on ");
    screen_console_puts(dev);
    screen_console_puts(" (minimal — superblock only, no directory entries: ls will show an empty volume; use mkfs.ext4 on the host for a full format)\n");  /* WP-09-FIX BUG-033 */
    return 0;
}

/* P1-7 FIX: fsck now does a basic FAT32 filesystem check.
 * Reads the boot sector, verifies BPB, counts FAT entries.
 * Not a full fsck (no cross-link detection, no orphan recovery),
 * but it's a real check that reports useful information. */
static int shell_cmd_fsck(const char *args) {
    /* If no device given, report mounted filesystems */
    if (!args || !args[0]) {
        screen_console_puts("fsck: checking mounted FAT32 filesystems\n");
        /* Walk mount table for FAT32 mounts */
        /* For now, just report that fsck needs a device argument */
        screen_console_puts("usage: fsck <device>\n");
        screen_console_puts("  e.g. fsck hda\n");
        return 0;
    }

    /* Parse device name */
    int drive = -1;
    if (strncmp(args, "hd", 2) == 0) {
        char c = args[2];
        if (c >= 'a' && c <= 'd' && args[3] == 0) drive = c - 'a';
    } else if (strncmp(args, "ata", 3) == 0) {
        char c = args[3];
        if (c >= '0' && c <= '3' && args[4] == 0) drive = c - '0';
    }
    if (drive < 0) {
        screen_console_puts("fsck: invalid device '");
        screen_console_puts(args);
        screen_console_puts("'\n");
        return 1;
    }

    /* Read boot sector */
    u8 boot[512];
    int n = driver_block_ata_read_sectors(drive, 0, 1, boot);
    if (n != 1) {
        screen_console_puts("fsck: cannot read boot sector\n");
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
        screen_console_puts("fsck: exFAT volume detected (fsck supports FAT32 only)\n");
        return 1;
    }
    {
        /* ext2/3/4: superblock starts at byte 1024 (LBA 2), magic 0xEF53
         * lives at superblock offset 0x38 = absolute byte 1080.  Require
         * two more sane fields so stray bytes in a wiped-but-not-zeroed
         * region cannot false-positive: log block size <= 6 and a
         * non-zero block count. */
        u8 sb[512];
        if (driver_block_ata_read_sectors(drive, 2, 1, sb) == 1) {
            u16 ext_magic = (u16)(sb[56] | (sb[57] << 8));      /* 1080-1024=56 */
            u32 lib_log_bs    = *(u32*)(sb + 24);
            u32 blocks    = *(u32*)(sb + 4);
            if (ext_magic == 0xEF53 && lib_log_bs <= 6 && blocks > 0) {
                screen_console_puts("fsck: ext2/3/4 volume detected (driver is read-only; fsck supports FAT32 only)\n");
                return 1;
            }
        }
    }

    /* Check FAT signature (0x55AA at offset 510) */
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        screen_console_puts("fsck: invalid boot signature (not 0x55AA)\n");
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
        screen_console_puts("fsck: not a FAT32 volume (fsck supports FAT32 only; got invalid BPB)\n");
        return 1;
    }

    /* P0fix2 BUG-0029 (A15-2): the fsck FAT32 report accumulates 8 fixed
     * strings plus numbers (154+ bytes on a typical volume) into what used
     * to be an 80-byte buffer — an unconditional 74-byte stack overflow on
     * every run; the second use below needs up to 98 bytes.  256 covers
     * both with margin. */
    char buf[256]; char num[20];
    /* WP-09-FIX BUG-021: report the device the user actually asked
     * about, not a hardcoded "hda". */
    screen_console_puts("fsck: FAT32 filesystem on ");
    screen_console_puts(args);
    screen_console_puts("\n");
    strcpy(buf, "  bytes_per_sector: "); u64_to_str(bytes_per_sec, num); strcat(buf, num);
    strcat(buf, "\n  sectors_per_cluster: "); u64_to_str(secs_per_clus, num); strcat(buf, num);
    strcat(buf, "\n  reserved_sectors: "); u64_to_str(reserved_sectors, num); strcat(buf, num);
    strcat(buf, "\n  num_fats: "); u64_to_str(num_fats, num); strcat(buf, num);
    strcat(buf, "\n  total_sectors: "); u64_to_str(total_sectors32, num); strcat(buf, num);
    strcat(buf, "\n  sectors_per_fat: "); u64_to_str(secs_per_fat32, num); strcat(buf, num);
    strcat(buf, "\n  root_dir_cluster: "); u64_to_str(root_dir_clus, num); strcat(buf, num);
    strcat(buf, "\n");
    screen_console_puts(buf);

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
        screen_console_puts("fsck: out of memory\n");
        return 1;
    }
    u32 entries_per_sector = bytes_per_sec / 4;  /* FAT32: 4 bytes per entry */
    u32 cluster_idx = 0;  /* global FAT entry index (0-based) */
    u32 max_scan = total_clusters + 2;  /* +2 for reserved entries 0,1 */
    if (max_scan > 100000) max_scan = 100000;

    for (u32 s = 0; s < secs_per_fat32 && cluster_idx < max_scan; s++) {
        n = driver_block_ata_read_sectors(drive, fat_start + s, 1, fat_buf);
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

    strcpy(buf, "  clusters: used="); u64_to_str(used, num); strcat(buf, num);
    strcat(buf, " free="); u64_to_str(free_clust, num); strcat(buf, num);
    strcat(buf, " bad="); u64_to_str(bad, num); strcat(buf, num);
    strcat(buf, " total="); u64_to_str(total_clusters, num); strcat(buf, num);
    strcat(buf, "\n  fsck: PASS (basic checks OK)\n");
    screen_console_puts(buf);
    return 0;
}

/* ---- sync: flush disk cache ---- */
static int shell_cmd_sync(const char *args) {
    (void)args;
    driver_block_cache_flush();
    screen_console_puts("disk cache flushed\n");
    return 0;
}

void shell_cmds_disk_register(void) {
    /* P1-18 FIX: df/du/mount/umount are registered in file_cmds.c (WP-05).
     * Only register WP-07-specific commands here to avoid duplicates. */
    shell_register_command_ex("lsblk", shell_cmd_lsblk, "list block devices", "WP-10a");
    shell_register_command_ex("parted", shell_cmd_parted, "show partition table (parted [dev])", "WP-10a");
    shell_register_command_ex("mkfs.fat32", shell_cmd_mkfs_fat32, "format FAT32 (mkfs.fat32 <dev> confirm)", "WP-10a");
    shell_register_command_ex("mkfs.exfat", shell_cmd_mkfs_exfat, "format exFAT (mkfs.exfat <dev> confirm)", "WP-10a");
    shell_register_command_ex("mkfs.ext4", shell_cmd_mkfs_ext4, "format ext4 (mkfs.ext4 <dev> confirm)", "WP-10a");
    shell_register_command_ex("fsck", shell_cmd_fsck, "filesystem check", "WP-10a");
    shell_register_command_ex("sync", shell_cmd_sync, "flush disk cache", "WP-10a");
}
