/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_mkfs.c
 * Purpose: the three format commands - see ocse_mkfs.h.
 *
 * FAT32/exFAT: the base blk_cmds.c implementations, verbatim logic
 * (only the output path changed to ocse_out).
 * ext4: written for real in this work package (4 KiB blocks, no
 * journal, sparse_super backups, root + lost+found, extents).
 */
#include "ocse_mkfs.h"
#include "ocse_i18n.h"
#include "ocse_part.h"
#include "driver_block_blk.h"
#include "driver_block_part.h"
#include "lib_string.h"
#include <stdint.h>

/* ---- the BUG-0239 confirm gate (migrated from blk_cmds.c) ---- */
int ocse_mkfs_gate(const char* cmd, const char* args,
                   char* devbuf, int devbuf_len) {
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && i < devbuf_len - 1) {
        devbuf[i] = args[i];
        i++;
    }
    if (args[i] && args[i] != ' ' && args[i] != '\t') {
        ocse_out(cmd);
        ocse_outln(": device name too long");
        return 1;
    }
    devbuf[i] = 0;
    if (!devbuf[0]) {
        ocse_out(cmd);
        ocse_out(": ");
        ocse_outln(ocse_tr(OCSE_T_M_WARN));
        ocse_out("  usage: ");
        ocse_out(cmd);
        ocse_outln(" <device> confirm");
        return 1;
    }
    while (args[i] == ' ' || args[i] == '\t') i++;
    char tail[16];
    int ti = 0;
    while (args[i] && ti < (int)sizeof(tail) - 1) tail[ti++] = args[i++];
    tail[ti] = 0;
    while (ti > 0 && (tail[ti - 1] == ' ' || tail[ti - 1] == '\t')) tail[--ti] = 0;
    if (args[i] || strcmp(tail, "confirm") != 0) {
        ocse_out(cmd);
        ocse_out(": ");
        ocse_out(ocse_tr(OCSE_T_M_WARN));
        ocse_out(" on '");
        ocse_out(devbuf);
        ocse_out("'");
        ocse_outln(ocse_tr(OCSE_T_M_CONFIRMED));
        ocse_out("  To really format, append the literal token 'confirm' as the last argument:");
        ocse_out("  usage: ");
        ocse_out(cmd);
        ocse_outln(" <device> confirm");
        return 1;
    }
    return 0;
}

static void wipe_raw(int dev_idx, u64 lba, u64 sectors) {
    static u8 z[512];
    memset(z, 0, sizeof(z));
    for (u64 s = 0; s < sectors; s++)
        driver_block_write_sectors_raw(dev_idx, lba + s, 1, z);
}

/* ================= mkfs.fat32 (base logic, verbatim) ================= */
int ocse_mkfs_fat32(int dev_idx) {
    driver_block_device_t *dev0 = driver_block_get_device(dev_idx);
    if (!dev0 || dev0->sectors < 40) return -1;
    u32 total_sect = (u32)dev0->sectors;
    u32 spc = 1;                       /* <= 260 MiB: 512 B clusters */
    if (total_sect > 532480u) spc = 8;         /* 260 MiB..8 GiB: 4 KiB */
    if (total_sect > 16384u * 1024u) spc = 16; /* 8 GiB..16 GiB: 8 KiB */
    if (total_sect > 32768u * 1024u) spc = 32;
    if (total_sect > 65536u * 1024u) spc = 64;
    u32 fat_secs = 1;
    for (int it = 0; it < 2; it++) {
        u32 data_secs = (total_sect > 32 + 2 * fat_secs)
                            ? total_sect - 32 - 2 * fat_secs : 1;
        u32 clusters = data_secs / spc;
        u32 fat_bytes = (clusters + 2 + 1) * 4;
        u32 need = (fat_bytes + 511) / 512;
        if (need > fat_secs) fat_secs = need;
        else if (it > 0) fat_secs = need;
    }
    if (fat_secs < 1) fat_secs = 1;
    u8 buf[512];
    memset(buf, 0, 512);
    buf[0] = 0xEB; buf[1] = 0x58; buf[2] = 0x90;
    memcpy(buf + 3, "MSWIN4.1", 8);
    *(u16*)(buf + 11) = 512;
    buf[13] = (u8)spc;
    *(u16*)(buf + 14) = 32;
    buf[16] = 2;
    *(u16*)(buf + 17) = 0;
    *(u16*)(buf + 19) = 0;
    buf[21] = 0xF8;
    *(u16*)(buf + 22) = 0;
    *(u16*)(buf + 24) = 63;
    *(u16*)(buf + 26) = 255;
    *(u32*)(buf + 28) = 0;
    driver_block_device_t *dev = driver_block_get_device(dev_idx);
    *(u32*)(buf + 32) = (u32)dev->sectors;
    *(u32*)(buf + 36) = fat_secs;
    *(u16*)(buf + 40) = 0;
    *(u16*)(buf + 42) = 0;
    *(u32*)(buf + 44) = 2;
    *(u16*)(buf + 48) = 1;
    *(u16*)(buf + 50) = 6;
    buf[64] = 0x80;
    buf[66] = 0x29;
    *(u32*)(buf + 67) = 0x12345678;
    memcpy(buf + 71, "NO NAME    ", 11);
    memcpy(buf + 82, "FAT32   ", 8);
    buf[510] = 0x55; buf[511] = 0xAA;
    driver_block_write_sectors_raw(dev_idx, 0, 1, buf);
    wipe_raw(dev_idx, 1, 31);
    u8 fat[512];
    memset(fat, 0, 512);
    *(u32*)(fat + 0) = 0x0FFFFFF8;
    *(u32*)(fat + 4) = 0x0FFFFFF8;
    *(u32*)(fat + 8) = 0x0FFFFFFF;
    wipe_raw(dev_idx, 32, 2 * (u64)fat_secs);
    for (int i = 0; i < 2; i++)
        driver_block_write_sectors_raw(dev_idx, 32 + (u32)i * fat_secs, 1, fat);
    u8 dir[512];
    memset(dir, 0, 512);
    driver_block_write_sectors_raw(dev_idx, 32 + 2 * fat_secs, 1, dir);
    return 0;
}

/* ================= mkfs.exfat (base logic, verbatim) ================= */
int ocse_mkfs_exfat(int dev_idx) {
    driver_block_device_t *devp = driver_block_get_device(dev_idx);
    if (!devp) return -1;
    if (devp->sectors < 65) return -1;
    u64 total_sectors = devp->sectors;
    u32 fat_offset = 32;
    u32 fat_length = 1;
    u32 heap_offset = fat_offset + fat_length;
    u64 cluster_count = 0;
    for (int it = 0; it < 8; it++) {
        heap_offset = fat_offset + fat_length;
        if (total_sectors <= heap_offset) break;
        u64 cc = total_sectors - heap_offset;
        u32 need = (u32)(((cc + 2) * 4 + 511) / 512);
        if (need == fat_length) { cluster_count = cc; break; }
        fat_length = need;
        cluster_count = cc;
    }
    if (cluster_count == 0) return -1;
    u8 buf[512];
    memset(buf, 0, 512);
    buf[0] = 0xEB; buf[1] = 0x76; buf[2] = 0x90;
    memcpy(buf + 3, "EXFAT   ", 8);
    *(u64*)(buf + 64) = 0;
    *(u64*)(buf + 72) = total_sectors;
    *(u32*)(buf + 80) = fat_offset;
    *(u32*)(buf + 84) = fat_length;
    *(u32*)(buf + 88) = heap_offset;
    *(u32*)(buf + 92) = (u32)cluster_count;
    *(u32*)(buf + 96) = 2;
    *(u16*)(buf + 100) = 0x0100;
    buf[108] = 9;
    buf[109] = 0;
    buf[110] = 1;
    buf[510] = 0x55; buf[511] = 0xAA;
    driver_block_write_sectors_raw(dev_idx, 0, 1, buf);
    wipe_raw(dev_idx, 1, fat_offset - 1);
    {
        u8 fat[512];
        memset(fat, 0, 512);
        *(u32*)(fat + 0) = 0xFFFFFFF8;
        *(u32*)(fat + 4) = 0xFFFFFFFF;
        *(u32*)(fat + 8) = 0xFFFFFFFF;
        driver_block_write_sectors_raw(dev_idx, fat_offset, 1, fat);
        memset(fat, 0, 512);
        for (u32 s = 1; s < fat_length; s++)
            driver_block_write_sectors_raw(dev_idx, fat_offset + s, 1, fat);
    }
    u8 rootdir[512];
    memset(rootdir, 0, 512);
    rootdir[0] = 0x83;
    memcpy(rootdir + 1, "OCOS       ", 11);
    driver_block_write_sectors_raw(dev_idx, heap_offset, 1, rootdir);
    return 0;
}

/* ================= mkfs.ext4 (real volume, WP-06b) =================
 * Profile: 4 KiB blocks, no journal, extents + filetype + sparse_super
 * + large_file, 32-byte group descriptors, 256-byte inodes, 2048
 * inodes/group (16 KiB/inode like mke2fs default). Single-group small
 * volumes (<= 128 MiB) get the classic layout:
 *   block 0: boot + superblock at byte 1024
 *   block 1: group descriptor table
 *   block 2: block bitmap, block 3: inode bitmap
 *   blocks 4..131: inode table (2048 x 256 B)
 *   block 132: root directory, block 133: lost+found
 * Larger volumes grow group g at block g*32768 with the metadata at
 * the group head and sparse_super backups (groups 1 and 3^k).
 */
#define EXT4_BSZ            4096
#define EXT4_BPB            8            /* blocks per 512-sector */
#define EXT4_BPG            32768        /* blocks per group */
#define EXT4_IPG            2048
#define EXT4_ISZ            256
#define EXT4_ROOT_INO       2
#define EXT4_LPF_INO        11
#define EXT4_ITBL_BLOCKS    (EXT4_IPG * EXT4_ISZ / EXT4_BSZ)

static u8 g_e4_block[EXT4_BSZ];          /* staging window (4 KiB) */

static int e4_write_block(int dev_idx, u64 block, const u8* data) {
    return driver_block_write_sectors_raw(dev_idx, block * EXT4_BPB,
                                          EXT4_BPB, data);
}
static int e4_read_block(int dev_idx, u64 block, u8* data) {
    return driver_block_read_sectors_raw(dev_idx, block * EXT4_BPB,
                                         EXT4_BPB, data);
}

static void e4_wr32(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}
static void e4_wr16(u8* p, u16 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8);
}

/* Superblock at byte 1024 (primary: block 0 of the volume).
 * Field offsets per the ext4 spec (linux fs/ext4/ext4.h):
 *   56  s_magic       = 0xEF53 (a volume whose magic is wrong is not
 *                      mountable - by Linux OR by our own fs_ext4, both
 *                      gate on it; live-caught in the batch-3 review:
 *                      the first draft wrote s_state here)
 *   58  s_state       = 1 (clean)
 *   60  s_errors      = 1 (continue) */
static void e4_fill_sb(u8* sb, u64 blocks, u32 groups,
                       u32 free_blocks, u32 free_inodes) {
    memset(sb, 0, 1024);
    e4_wr32(sb + 0,  groups * EXT4_IPG);          /* s_inodes_count */
    e4_wr32(sb + 4,  (u32)blocks);                /* s_blocks_count_lo */
    e4_wr32(sb + 8,  0);                          /* s_r_blocks_count */
    e4_wr32(sb + 12, free_blocks);
    e4_wr32(sb + 16, free_inodes);
    e4_wr32(sb + 20, 0);                          /* s_first_data_block */
    e4_wr32(sb + 24, 2);                          /* log block size = 4096 */
    e4_wr32(sb + 28, 2);                          /* log cluster size */
    e4_wr32(sb + 32, EXT4_BPG);
    e4_wr32(sb + 36, EXT4_BPG);
    e4_wr32(sb + 40, EXT4_IPG);
    e4_wr16(sb + 56, 0xEF53);                     /* s_magic (see above) */
    e4_wr16(sb + 58, 1);                          /* s_state: clean */
    e4_wr16(sb + 60, 1);                          /* s_errors: continue */
    e4_wr32(sb + 76, 1);                          /* s_rev_level dynamic */
    e4_wr32(sb + 84, 11);                         /* s_first_ino = 11 */
    e4_wr16(sb + 88, EXT4_ISZ);
    e4_wr32(sb + 92, 0);                          /* compat: none */
    e4_wr32(sb + 96, 0x40 | 0x2);                 /* incompat: extents|filetype */
    e4_wr32(sb + 100, 0x1 | 0x200);               /* ro: sparse_super|large_file */
    /* s_uuid: deterministic OCOS volume id */
    static const u8 uuid[16] = {
        0x4F, 0x43, 0x4F, 0x53, 0x2D, 0x65, 0x78, 0x74,
        0x34, 0x2D, 0x33, 0x2E, 0x30, 0x2E, 0x30, 0x00 };
    memcpy(sb + 104, uuid, 16);
    memcpy(sb + 120, "ocos-root\0\0\0\0\0\0", 16); /* s_volume_name */
    e4_wr16(sb + 254, 32);                         /* s_desc_size = 32 */
}

/* Group descriptor (32-byte classic layout). */
static void e4_fill_gd(u8* gd, u32 bitmap_blk, u32 ibitmap_blk,
                       u32 itable_blk, u32 free_blocks, u32 free_inodes,
                       u32 used_dirs) {
    memset(gd, 0, 32);
    e4_wr32(gd + 0,  bitmap_blk);
    e4_wr32(gd + 4,  ibitmap_blk);
    e4_wr32(gd + 8,  itable_blk);
    e4_wr16(gd + 12, (u16)free_blocks);
    e4_wr16(gd + 14, (u16)free_inodes);
    e4_wr16(gd + 16, (u16)used_dirs);
}

/* Sparse_super backup set: group 0 (primary) plus 1, 3^k, 5^k and 7^k
 * (the standard mke2fs placement; the first draft also took 3^k+1,
 * which is a GNU extension Linux does not read - backups there are
 * dead weight, so the set matches e2fsprogs exactly). */
static int e4_is_backup_group(u32 g) {
    if (g == 0) return 0;
    if (g == 1) return 1;
    for (u32 p = 3; p <= g; p *= 3) if (p == g) return 1;
    for (u32 p = 5; p <= g; p *= 5) if (p == g) return 1;
    for (u32 p = 7; p <= g; p *= 7) if (p == g) return 1;
    return 0;
}

static void e4_dirent(u8* dir, int* off, u32 ino, const char* name,
                      int total_rec) {
    int nl = (int)strlen(name);
    /* total_rec is the EXACT record length the caller wants: 12 for
     * "."/"..", block-end for the LAST entry (a trailing dirent that
     * does not consume the block tail leaves a zero rec_len hole -
     * e2fsck flags it and our own dirent walker stops there;
     * live-caught in the batch-3 battery). */
    int rec = total_rec;
    e4_wr32(dir + *off, ino);
    e4_wr16(dir + *off + 4, (u16)rec);
    dir[*off + 6] = (u8)nl;
    dir[*off + 7] = 2;                      /* file_type = directory */
    for (int i = 0; i < nl; i++) dir[*off + 8 + i] = (u8)name[i];
    *off += rec;
}

int ocse_mkfs_ext4(int dev_idx) {
    driver_block_device_t* dev = driver_block_get_device(dev_idx);
    if (!dev) return -1;
    u64 blocks_total = dev->sectors / EXT4_BPB;
    if (blocks_total < 200) return -1;     /* need >= ~800 KiB */
    u32 groups = (u32)((blocks_total + EXT4_BPG - 1) / EXT4_BPG);
    if (groups > 128) groups = 128;         /* GDT cap: 1 block of 32B GDs */
    /* Every group must honestly fit its metadata (block bitmap + inode
     * bitmap + inode table [+ sb/GDT copies in backup groups]). A last
     * partial group smaller than its metadata would produce an
     * underflowed (huge) bg_free_blocks_count - the allocator in
     * ocse_ext4w.c trusts those counts, so instead of lying, shrink the
     * group count until the tail group fits; the dropped tail blocks
     * (at most ~520 KiB) are simply not part of the volume. */
    while (groups > 1) {
        u32 last = groups - 1;
        u32 meta = (e4_is_backup_group(last) ? 4u : 2u) + EXT4_ITBL_BLOCKS;
        u64 fb = blocks_total - (u64)last * EXT4_BPG;
        if (fb >= meta) break;
        groups--;
    }
    /* Blocks the fs actually covers/claims (s_blocks_count must equal
     * what the group bitmaps describe - never more). */
    u64 covered = blocks_total;
    if (covered > (u64)groups * EXT4_BPG) covered = (u64)groups * EXT4_BPG;

    /* group 0 metadata block plan */
    u64 gdt_block = 1;
    u64 bbitmap = 2;
    u64 ibitmap = 3;
    u64 itable = 4;
    u64 first_free = itable + EXT4_ITBL_BLOCKS;   /* first data block */
    u32 used_g0 = (u32)(first_free + 2);          /* + root dir + lost+found */

    /* ---- superblock (built after the honest accounting below; the
     * free counts must cover EVERY group, not just group 0 - the first
     * draft only subtracted group 0's metadata, so multi-group volumes
     * over-reported free space by the metadata size of every other
     * group and e2fsck/df would have disagreed with our own bitmaps) */
    u8 sb[1024];

    /* ---- group descriptor table + honest per-group accounting ---- */
    memset(g_e4_block, 0, sizeof(g_e4_block));
    u32 used_total = 0;
    for (u32 g = 0; g < groups; g++) {
        u8 gd[32];
        u64 base = (u64)g * EXT4_BPG;
        u64 rem = covered > base ? covered - base : 0;
        u32 fb = rem > EXT4_BPG ? EXT4_BPG : (u32)rem;
        u32 gd_bmp, gd_ibmp, gd_itbl, used_dirs, meta;
        if (g == 0) {
            gd_bmp = (u32)bbitmap;
            gd_ibmp = (u32)ibitmap;
            gd_itbl = (u32)itable;
            used_dirs = 1;
            meta = used_g0;
        } else if (e4_is_backup_group(g)) {
            gd_bmp = (u32)base + 2;
            gd_ibmp = (u32)base + 3;
            gd_itbl = (u32)base + 4;
            used_dirs = 0;
            meta = 4 + EXT4_ITBL_BLOCKS;
        } else {
            gd_bmp = (u32)base;
            gd_ibmp = (u32)base + 1;
            gd_itbl = (u32)base + 2;
            used_dirs = 0;
            meta = 2 + EXT4_ITBL_BLOCKS;
        }
        if (fb < meta) return -1;      /* guarded by the shrink loop */
        used_total += meta;
        e4_fill_gd(gd, gd_bmp, gd_ibmp, gd_itbl, fb - meta,
                   g == 0 ? (u32)(EXT4_IPG - 11) : EXT4_IPG, used_dirs);
        memcpy(g_e4_block + g * 32, gd, 32);
    }
    u32 free_blocks = (u32)(covered - used_total);
    u32 free_inodes = EXT4_IPG * groups - 11;   /* inodes 1..10 reserved, 2+11 used */
    e4_fill_sb(sb, covered, groups, free_blocks, free_inodes);
    if (e4_write_block(dev_idx, gdt_block, g_e4_block) != 0) return -1;

    /* ---- block 0: boot + superblock ---- */
    memset(g_e4_block, 0, sizeof(g_e4_block));
    memcpy(g_e4_block + 1024, sb, 1024);
    if (e4_write_block(dev_idx, 0, g_e4_block) != 0) return -1;

    /* ---- block 2: block bitmap (group 0) ----
     * meta [0, used_g0) used; tail beyond the volume's real block count
     * ([covered, BPG) on a single-group volume) used too - the
     * allocator must never hand out a block the volume does not have.
     * (Live-caught in the batch-3 battery: the first draft marked
     * [0, covered) when covered < BPG, i.e. the WHOLE volume, so the
     * bitmap said zero free blocks while the GD said 8058 - the write
     * path would have failed ENOSPC on the first allocation.) */
    memset(g_e4_block, 0, sizeof(g_e4_block));
    {
        for (u64 b = 0; b < used_g0; b++)
            g_e4_block[b / 8] |= (u8)(1u << (b % 8));
        if (covered < EXT4_BPG)
            for (u64 b = (u32)covered; b < EXT4_BPG; b++)
                g_e4_block[b / 8] |= (u8)(1u << (b % 8));
    }
    if (e4_write_block(dev_idx, bbitmap, g_e4_block) != 0) return -1;

    /* ---- block 3: inode bitmap (group 0) ---- */
    memset(g_e4_block, 0, sizeof(g_e4_block));
    /* inodes 1..10 reserved (bits 0..9) + 11 lost+found (bit 10) +
     * root uses inode 2 (bit 1) - all inside the reserved 1..11 set. */
    for (int b = 0; b < 11; b++)
        g_e4_block[b / 8] |= (u8)(1u << (b % 8));
    if (e4_write_block(dev_idx, ibitmap, g_e4_block) != 0) return -1;

    /* ---- blocks 4..: inode table (group 0) ----
     * Slot i holds inode i+1 (ext4 numbers inodes from 1; e2fsprogs and
     * the read/write drivers both address inode N at slot N-1). B3's
     * battery never read the volume back through the driver, so the
     * original loop compared the SLOT INDEX against the inode NUMBER -
     * root(2) landed in inode 3's slot and every post-mount read saw
     * zeros. Live-caught by the b4 file battery ("cd /mnt: not a
     * directory"; the mount root stat'd as a size-0 FILE). */
    for (u32 i = 0; i < EXT4_IPG; i++) {
        u32 ino_num = i + 1;
        u64 blk = itable + (u64)(i * EXT4_ISZ) / EXT4_BSZ;
        u64 off = (u64)(i * EXT4_ISZ) % EXT4_BSZ;
        if (off == 0) {
            memset(g_e4_block, 0, sizeof(g_e4_block));
        }
        if (ino_num == EXT4_ROOT_INO) {
            /* root directory inode */
            u8* ino = g_e4_block + off;
            e4_wr16(ino + 0, 0x41ED);          /* drwxr-xr-x */
            e4_wr16(ino + 2, 0);               /* uid */
            e4_wr32(ino + 4, EXT4_BSZ);         /* size */
            e4_wr16(ino + 26, 3);               /* links_count: . .. lpf */
            e4_wr32(ino + 28, EXT4_BSZ / 512);  /* i_blocks (512B units) */
            e4_wr32(ino + 32, 0x80000);        /* EXTENTS flag */
            /* extent header at i_block[0] */
            e4_wr16(ino + 40, 0xF30A);          /* eh_magic */
            e4_wr16(ino + 42, 1);               /* eh_entries */
            e4_wr16(ino + 44, 4);               /* eh_max: i_block root holds 4 extents (WP-06b) */
            e4_wr16(ino + 46, 0);               /* eh_depth */
            e4_wr32(ino + 48, 0);               /* eh_generation */
            /* extent: logical 0, len 1, phys = first_free (root dir) */
            e4_wr32(ino + 52, 0);
            e4_wr16(ino + 56, 1);
            e4_wr16(ino + 58, (u16)((first_free) >> 32));
            e4_wr32(ino + 60, (u32)(first_free));
        } else if (ino_num == EXT4_LPF_INO) {
            u8* ino = g_e4_block + off;
            e4_wr16(ino + 0, 0x41C0);          /* drwx------ */
            e4_wr32(ino + 4, EXT4_BSZ);
            e4_wr16(ino + 26, 2);               /* . .. */
            e4_wr32(ino + 28, EXT4_BSZ / 512);
            e4_wr32(ino + 32, 0x80000);
            e4_wr16(ino + 40, 0xF30A);
            e4_wr16(ino + 42, 1);
            e4_wr16(ino + 44, 4);               /* eh_max: i_block root holds 4 extents */
            e4_wr16(ino + 46, 0);
            e4_wr32(ino + 48, 0);
            e4_wr32(ino + 52, 0);
            e4_wr16(ino + 56, 1);
            e4_wr16(ino + 58, (u16)((first_free + 1) >> 32));
            e4_wr32(ino + 60, (u32)(first_free + 1));
        }
        if (off == EXT4_BSZ - EXT4_ISZ || i == EXT4_IPG - 1) {
            if (e4_write_block(dev_idx, blk, g_e4_block) != 0) return -1;
        }
    }

    /* ---- root directory block ---- */
    memset(g_e4_block, 0, sizeof(g_e4_block));
    {
        int off = 0;
        e4_dirent(g_e4_block, &off, EXT4_ROOT_INO, ".", 12);
        e4_dirent(g_e4_block, &off, EXT4_ROOT_INO, "..", 12);
        e4_dirent(g_e4_block, &off, EXT4_LPF_INO, "lost+found",
                  EXT4_BSZ - off);
    }
    if (e4_write_block(dev_idx, first_free, g_e4_block) != 0) return -1;

    /* ---- lost+found directory block ---- */
    memset(g_e4_block, 0, sizeof(g_e4_block));
    {
        int off = 0;
        e4_dirent(g_e4_block, &off, EXT4_LPF_INO, ".", 12);
        e4_dirent(g_e4_block, &off, EXT4_ROOT_INO, "..", 12);
    }
    if (e4_write_block(dev_idx, first_free + 1, g_e4_block) != 0) return -1;

    /* ---- per-group bitmaps/tables for groups > 0 ----
     * Backup groups: [sb copy][GDT copy][bitmap][ibitmap][itable...] */
    for (u32 g = 1; g < groups; g++) {
        u64 base = (u64)g * EXT4_BPG;
        u32 backup = (u32)e4_is_backup_group(g);
        u32 meta_used = (backup ? 4u : 2u) + EXT4_ITBL_BLOCKS;
        u32 bmp_off = backup ? 2u : 0u;
        u64 rem = covered > base ? covered - base : 0;
        u32 fb = rem > EXT4_BPG ? EXT4_BPG : (u32)rem;
        /* block bitmap: meta used + tail (a partial last group's bits
         * beyond its real block count) used */
        memset(g_e4_block, 0, sizeof(g_e4_block));
        for (u32 b = 0; b < meta_used; b++)
            g_e4_block[b / 8] |= (u8)(1u << (b % 8));
        if (fb < EXT4_BPG)
            for (u32 b = fb; b < EXT4_BPG; b++)
                g_e4_block[b / 8] |= (u8)(1u << (b % 8));
        if (e4_write_block(dev_idx, base + bmp_off, g_e4_block) != 0)
            return -1;
        /* inode bitmap: empty */
        memset(g_e4_block, 0, sizeof(g_e4_block));
        if (e4_write_block(dev_idx, base + bmp_off + 1, g_e4_block) != 0)
            return -1;
        /* inode table: zeroed */
        memset(g_e4_block, 0, sizeof(g_e4_block));
        for (u32 b = 0; b < EXT4_ITBL_BLOCKS; b++)
            if (e4_write_block(dev_idx, base + bmp_off + 2 + b,
                               g_e4_block) != 0)
                return -1;
    }

    /* ---- sparse_super backups (groups 1, 3^k, 5^k, 7^k): sb + GDT
     * copies at the group head (before that group's bitmap). ---- */
    for (u32 g = 1; g < groups; g++) {
        if (!e4_is_backup_group(g)) continue;
        u64 base = (u64)g * EXT4_BPG;
        memset(g_e4_block, 0, sizeof(g_e4_block));
        memcpy(g_e4_block, sb, 1024);
        if (e4_write_block(dev_idx, base, g_e4_block) != 0) return -1;
        if (e4_read_block(dev_idx, gdt_block, g_e4_block) != 0) return -1;
        if (e4_write_block(dev_idx, base + 1, g_e4_block) != 0) return -1;
    }
    return 0;
}

/* ---- Partition type sync ----
 * After formatting, the partition's type must say what the volume now
 * is (what mkfs on Linux leaves for parted to see):
 *   MBR: FAT32 LBA=0x0C, exFAT/NTFS-generic=0x07, Linux=0x83
 *   GPT: Microsoft basic data {EBD0A0A2-...} for FAT/exFAT,
 *        Linux filesystem {0FC63DAF-...} for ext4 */
static const u8 g_mk_guid_ms_basic[16] = {    /* EBD0A0A2-B9E5-4433-... */
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};
static const u8 g_mk_guid_linux_fs[16] = {    /* 0FC63DAF-8483-4772-... */
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

void ocse_mkfs_sync_mbr_type(const char* dev_name, int fmt) {
    if (!dev_name || !dev_name[0]) return;
    int idx = driver_block_find_device(dev_name);
    if (idx < 0) return;
    driver_block_device_t* dev = driver_block_get_device(idx);
    if (!dev) return;
    /* Only partition children carry a partition type worth syncing. */
    u32 len = 0;
    while (dev_name[len]) len++;
    u32 end = len;
    while (end > 0 && dev_name[end - 1] >= '0' && dev_name[end - 1] <= '9') end--;
    if (end == 0 || end == len) return;
    if (dev_name[end - 1] != 'p') return;
    char parent[16];
    for (u32 i = 0; i < end - 1 && i < sizeof(parent) - 1; i++)
        parent[i] = dev_name[i];
    parent[end - 1] = 0;
    int pno = 0;
    for (u32 i = end; i < len; i++) pno = pno * 10 + (dev_name[i] - '0');
    if (pno < 1 || pno > 4) return;
    int pidx = driver_block_find_device(parent);
    if (pidx < 0) return;
    /* AUD-2e-001: pno is the DENSE partition number parsed from
     * "<parent>pN"; resolve it to the raw on-disk slot (MBR 1-4 /
     * GPT entry index) through the parse table - with empty slots
     * before it, using pno as a slot index patched the wrong entry. */
    driver_block_part_table_t tbl;
    if (driver_block_part_parse(pidx, &tbl) != 0) return;
    if (pno > tbl.count || !tbl.parts[pno - 1].present) return;
    int slot = tbl.parts[pno - 1].slot;
    /* GPT partitions take a type GUID instead of an MBR type byte. */
    if (tbl.table_type == PART_TYPE_GPT) {
        const u8* guid = fmt == OCSE_FMT_EXT4 ? g_mk_guid_linux_fs
                                              : g_mk_guid_ms_basic;
        ocse_part_gpt_set_type(parent, pno, guid);
        return;
    }
    if (slot < 1 || slot > 4) return;      /* defense: raw slot out of range */
    u8 sec[512];
    if (driver_block_read_sectors_raw(pidx, 0, 1, sec) != 0) return;
    u8 type = fmt == OCSE_FMT_FAT32 ? 0x0C :
              fmt == OCSE_FMT_EXFAT ? 0x07 : 0x83;
    u8* p = sec + 446 + (slot - 1) * 16;   /* AUD-2e-001: raw slot */
    if (p[4] != type) {
        p[4] = type;
        driver_block_write_sectors_raw(pidx, 0, 1, sec);
    }
}
