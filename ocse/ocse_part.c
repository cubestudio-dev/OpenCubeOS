/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_part.c
 * Purpose: partition editing - see ocse_part.h for the command list
 * and the never-move / never-shrink / report-when-full contract.
 *
 * Implementation notes:
 *  - MBR changes are read-modify-write on sector 0: untouched slots,
 *    boot flags and the NT-signature area survive byte-for-byte.
 *  - GPT changes rewrite the entry array and both headers (primary at
 *    LBA 1, backup at the backup-LBA the primary header names), each
 *    with freshly computed IEEE-CRC32 checksums.
 *  - After any table change the OCSE disk inventory is re-scanned so
 *    `lsdisk` reflects reality immediately.
 */
#include "ocse_part.h"
#include "ocse_atapi.h"
#include "ocse_disk.h"
#include "ocse_i18n.h"
#include "driver_block_blk.h"
#include "driver_block_part.h"
#include "fs_vfs.h"
#include "lib_string.h"
#include <stdint.h>

static u8  g_sec[512];
static u8  g_gpt_ent[128];          /* one GPT entry */
static u8  g_gpt_hdr[512];

static int is_cd(const char* n) { return ocse_atapi_is_cd_name(n); }

/* Does `name` reference a partition child ("<parent>pN")? */
static int is_child_dev(const char* name) {
    u32 len = 0;
    while (name[len]) len++;
    u32 end = len;
    while (end > 0 && name[end - 1] >= '0' && name[end - 1] <= '9') end--;
    if (end == 0 || end == len) return 0;
    if (name[end - 1] != 'p') return 0;
    char parent[16];
    for (u32 i = 0; i < end - 1 && i < sizeof(parent) - 1; i++)
        parent[i] = name[i];
    parent[end - 1] = 0;
    return driver_block_find_device(parent) >= 0;
}

int ocse_part_disk_idx(const char* disk) {
    if (!disk || !disk[0]) return -1;
    if (is_cd(disk)) return -1;
    if (is_child_dev(disk)) return -1;
    int idx = driver_block_find_device(disk);
    driver_block_device_t* dev = idx >= 0 ? driver_block_get_device(idx) : NULL;
    if (!dev || !dev->present) return -1;
    return idx;
}

static void putnum(u64 v) {
    char n[24];
    u64_to_str(v, n);
    ocse_out(n);
}

static int dev_is_mounted(const char* name) {
    fs_vfs_mount_info_t mounts[8];
    int n = fs_vfs_get_mounts(mounts, 8);
    for (int i = 0; i < n; i++)
        if (strcmp(mounts[i].device, name) == 0) return 1;
    return 0;
}

static void child_name(const char* disk, int part_no, char* out, int cap) {
    int i = 0;
    while (disk[i] && i < cap - 8) { out[i] = disk[i]; i++; }
    out[i++] = 'p';
    char n[8];
    u64_to_str((u64)part_no, n);
    int j = 0;
    while (n[j] && i < cap - 1) out[i++] = n[j++];
    out[i] = 0;
}

/* ---- MBR slot helpers (sector 0, read-modify-write) ---- */

static int mbr_read(int dev_idx) {
    return driver_block_read_sectors_raw(dev_idx, 0, 1, g_sec);
}
static int mbr_write(int dev_idx) {
    return driver_block_write_sectors_raw(dev_idx, 0, 1, g_sec);
}
static u32 rd32_(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void wr32_(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* ---- GPT helpers ---- */

static const u8 g_guid_linux_fs[16] = {   /* 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

static int gpt_read_hdr(int dev_idx, u64* entries_lba, u32* nent,
                        u32* esz, u64* backup_lba, u64* last_usable) {
    if (driver_block_read_sectors_raw(dev_idx, 1, 1, g_gpt_hdr) != 0)
        return -1;
    if (memcmp(g_gpt_hdr, "EFI PART", 8) != 0) return -1;
    u32 hsize = rd32_(g_gpt_hdr + 12);
    u32 crc   = rd32_(g_gpt_hdr + 16);
    wr32_(g_gpt_hdr + 16, 0);
    if (hsize != 92 || driver_block_part_crc32(g_gpt_hdr, 92) != crc)
        return -1;
    wr32_(g_gpt_hdr + 16, crc);
    *entries_lba  = rd32_(g_gpt_hdr + 72) | ((u64)rd32_(g_gpt_hdr + 76) << 32);
    *nent         = rd32_(g_gpt_hdr + 80);
    *esz          = rd32_(g_gpt_hdr + 84);
    *backup_lba   = rd32_(g_gpt_hdr + 32) | ((u64)rd32_(g_gpt_hdr + 36) << 32);
    *last_usable  = rd32_(g_gpt_hdr + 48) | ((u64)rd32_(g_gpt_hdr + 52) << 32);
    if (*esz != 128 || *nent == 0 || *nent > 128) return -1;
    return 0;
}

static int gpt_read_entry(int dev_idx, u64 entries_lba, u32 esz, int i) {
    u64 lba = entries_lba + ((u64)i * esz) / 512;
    if (driver_block_read_sectors_raw(dev_idx, lba, 1, g_sec) != 0) return -1;
    memcpy(g_gpt_ent, g_sec + ((u32)(i * esz) & 511u), 128);
    return 0;
}

static int gpt_write_entry(int dev_idx, u64 entries_lba, u32 esz, int i) {
    u64 lba = entries_lba + ((u64)i * esz) / 512;
    if (driver_block_read_sectors_raw(dev_idx, lba, 1, g_sec) != 0) return -1;
    memcpy(g_sec + ((u32)(i * esz) & 511u), g_gpt_ent, 128);
    return driver_block_write_sectors_raw(dev_idx, lba, 1, g_sec);
}

/* Rebuild the entries-array CRC and write both headers. The entry
 * array is gathered into a static window (128 entries x 128 B =
 * 16 KiB) and checksummed in one pass - driver_block_part_crc32 is
 * a one-shot API and the vendored layer must not grow helpers. */
static u8 g_gpt_arr[128 * 128];
static int gpt_refresh_crcs(int dev_idx, u64 entries_lba, u32 nent,
                            u32 esz, u64 backup_lba) {
    u32 array_bytes = nent * esz;
    u32 done = 0;
    while (done < array_bytes) {
        u64 lba = entries_lba + done / 512;
        if (driver_block_read_sectors_raw(dev_idx, lba, 1,
                                          g_gpt_arr + (done & ~511u)) != 0)
            return -1;
        done += 512;
    }
    u32 crc = driver_block_part_crc32(g_gpt_arr, array_bytes);
    /* Update both headers: primary + backup. */
    for (int which = 0; which < 2; which++) {
        u64 hlba = which == 0 ? 1 : backup_lba;
        if (driver_block_read_sectors_raw(dev_idx, hlba, 1, g_gpt_hdr) != 0)
            return -1;
        if (memcmp(g_gpt_hdr, "EFI PART", 8) != 0) return -1;
        wr32_(g_gpt_hdr + 88, crc);              /* partition entry array CRC */
        u32 hsize = rd32_(g_gpt_hdr + 12);
        wr32_(g_gpt_hdr + 16, 0);
        u32 newcrc = driver_block_part_crc32(g_gpt_hdr, hsize);
        wr32_(g_gpt_hdr + 16, newcrc);
        if (driver_block_write_sectors_raw(dev_idx, hlba, 1, g_gpt_hdr) != 0)
            return -1;
    }
    return 0;
}

/* ---- commands ---- */

int ocse_part_lsdisk(void) {
    if (g_ocse_ndisks == 0) {
        ocse_outln(ocse_tr(OCSE_T_NO_DISK));
        return 1;
    }
    for (int d = 0; d < g_ocse_ndisks; d++) {
        ocse_disk_t* dk = &g_ocse_disks[d];
        ocse_out("  ");
        ocse_out(dk->name);
        ocse_out(dk->table_kind == 2 ? " [GPT] " :
                 dk->table_kind == 1 ? " [MBR] " : " [no table] ");
        putnum(dk->mb); ocse_out(" MiB, ");
        putnum((u64)dk->nparts); ocse_out(" part(s)");
        ocse_outln("");
        if (dk->space_low) {
            ocse_out("    (!) ");
            ocse_outln(ocse_tr(OCSE_T_SPACE_LOW));
        }
    }
    return 0;
}

int ocse_part_lspart(const char* disk) {
    int dev_idx = ocse_part_disk_idx(disk);
    if (dev_idx < 0) {
        ocse_out(ocse_tr(OCSE_T_P_NODISK));
        ocse_outln(disk);
        return 1;
    }
    ocse_disk_scan();
    const ocse_disk_t* dk = NULL;
    for (int i = 0; i < g_ocse_ndisks; i++)
        if (strcmp(g_ocse_disks[i].name, disk) == 0) dk = &g_ocse_disks[i];
    if (!dk) {
        ocse_out(ocse_tr(OCSE_T_P_NODISK));
        ocse_outln(disk);
        return 1;
    }
    if (dk->nparts == 0) {
        ocse_outln(ocse_tr(OCSE_T_P_NOPART));
        return 0;
    }
    for (int p = 0; p < dk->nparts; p++) {
        const ocse_part_t* pt = &dk->parts[p];
        ocse_out("  #");
        putnum((u64)(p + 1));
        ocse_out(" ");
        ocse_out(ocse_fs_name(pt->fs));
        ocse_out(" start=");
        putnum(pt->start_lba / 2048);
        ocse_out("MiB end=");
        putnum((pt->start_lba + pt->sectors) / 2048);
        ocse_out("MiB size=");
        putnum(pt->mb);
        ocse_out("MiB type=0x");
        char n[8];
        u64_to_hex((u64)pt->mbr_type, n, 2);     /* WP06b: hex, not decimal */
        ocse_out(n);
        ocse_outln("");
    }
    return 0;
}

int ocse_part_parted(const char* disk) {
    int dev_idx = ocse_part_disk_idx(disk);
    if (dev_idx < 0) {
        ocse_out(ocse_tr(OCSE_T_P_NODISK));
        ocse_outln(disk);
        return 1;
    }
    driver_block_part_table_t tbl;
    if (driver_block_part_parse(dev_idx, &tbl) != 0) {
        ocse_outln(ocse_tr(OCSE_T_P_NOTABLE));
        return 1;
    }
    driver_block_device_t* dev = driver_block_get_device(dev_idx);
    ocse_out("disk ");
    ocse_out(disk);
    ocse_out(tbl.table_type == PART_TYPE_GPT ? ": GPT, " : ": MBR, ");
    putnum(dev->sectors / 2);
    ocse_out(" KiB sectors, ");
    putnum((u64)tbl.count);
    ocse_outln(" partition(s):");
    for (int i = 0; i < tbl.count; i++) {
        partition_t* pt = &tbl.parts[i];
        if (!pt->present) continue;
        ocse_out("  #");
        putnum((u64)(i + 1));
        ocse_out(" boot=");
        ocse_out(pt->bootable ? "y" : "n");
        ocse_out(" start=");
        putnum(pt->start_lba);
        ocse_out(" sectors=");
        putnum(pt->sectors);
        ocse_outln("");
    }
    return 0;
}

/* Shared range validation for mkpart. Returns 0 and fills the LBA
 * pair on success; prints the reason otherwise. */
static int mkpart_range(int dev_idx, u64 start_mib, u64 end_mib,
                        u64* start_lba, u64* end_lba) {
    driver_block_device_t* dev = driver_block_get_device(dev_idx);
    if (start_mib == 0) start_mib = 1;          /* LBA 2048 alignment */
    /* Live-caught in the batch-2 battery: the bound used to be
     * sectors/2048 + 1, letting end_mib == disk_MiB + 1 pass and push
     * end_lba past the last sector. The exact bound is disk_MiB:
     * end_mib * 2048 - 1 <= sectors - 1. */
    if (end_mib <= start_mib || end_mib > dev->sectors / 2048) {
        ocse_outln(ocse_tr(OCSE_T_P_BADRANGE));
        return 1;
    }
    *start_lba = start_mib * 2048;
    *end_lba   = end_mib * 2048 - 1;             /* inclusive */
    return 0;
}

static int mbr_mkpart(int dev_idx, u64 start_lba, u64 end_lba) {
    if (mbr_read(dev_idx) != 0) return 1;
    int slot = -1;
    for (int i = 0; i < 4; i++)
        if (g_sec[446 + i * 16 + 4] == 0) { slot = i; break; }
    if (slot < 0) {
        ocse_outln(ocse_tr(OCSE_T_P_TABLEFULL));
        return 1;
    }
    /* Overlap check against the surviving slots. */
    for (int i = 0; i < 4; i++) {
        if (g_sec[446 + i * 16 + 4] == 0) continue;
        u64 s = rd32_(g_sec + 446 + i * 16 + 8);
        u32 n = rd32_(g_sec + 446 + i * 16 + 12);
        u64 e = s + n - 1;
        if (start_lba <= e && s <= end_lba) {
            ocse_outln(ocse_tr(OCSE_T_P_OVERLAP));
            return 1;
        }
    }
    u8* p = g_sec + 446 + slot * 16;
    p[0] = 0x00;                                 /* not bootable yet */
    p[1] = 0xFE; p[2] = 0xFF; p[3] = 0xFF;       /* CHS start filler */
    p[4] = 0x83;                                 /* Linux filesystem */
    p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;       /* CHS end filler */
    wr32_(p + 8,  (u32)start_lba);
    wr32_(p + 12, (u32)(end_lba - start_lba + 1));
    /* Live-caught in the batch-2 battery: a BLANK disk carries no boot
     * signature, and every consumer (our own parse path, the stage-22
     * child scan, OCBL) gates on 0xAA55. Stamp it when missing - a
     * table without the signature is not a table. */
    if (g_sec[510] != 0x55 || g_sec[511] != 0xAA) {
        g_sec[510] = 0x55;
        g_sec[511] = 0xAA;
    }
    if (mbr_write(dev_idx) != 0) return 1;
    return 0;
}

static int gpt_mkpart(int dev_idx, u64 start_lba, u64 end_lba) {
    u64 entries_lba, backup_lba, last_usable;
    u32 nent, esz;
    if (gpt_read_hdr(dev_idx, &entries_lba, &nent, &esz,
                     &backup_lba, &last_usable) != 0) {
        ocse_outln(ocse_tr(OCSE_T_P_NOTABLE));
        return 1;
    }
    if (end_lba > last_usable) {
        ocse_outln(ocse_tr(OCSE_T_P_NOFREE));
        return 1;
    }
    int slot = -1;
    for (u32 i = 0; i < nent; i++) {
        if (gpt_read_entry(dev_idx, entries_lba, esz, (int)i) != 0) return 1;
        int used = 0;
        for (int b = 0; b < 16; b++) if (g_gpt_ent[b]) { used = 1; break; }
        if (!used) { slot = (int)i; break; }
        /* overlap check */
        u64 s = rd32_(g_gpt_ent + 32) | ((u64)rd32_(g_gpt_ent + 36) << 32);
        u64 e = rd32_(g_gpt_ent + 40) | ((u64)rd32_(g_gpt_ent + 44) << 32);
        if (start_lba <= e && s <= end_lba) {
            ocse_outln(ocse_tr(OCSE_T_P_OVERLAP));
            return 1;
        }
    }
    if (slot < 0) {
        ocse_outln(ocse_tr(OCSE_T_P_TABLEFULL));
        return 1;
    }
    memset(g_gpt_ent, 0, 128);
    for (int b = 0; b < 16; b++) g_gpt_ent[b] = g_guid_linux_fs[b];
    /* Deterministic unique GUID: OCOS-prefix + slot (reproducible
     * builds beat randomness here). */
    static const u8 base[16] = {
        0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x47, 0x48,
        0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0x00 };
    for (int b = 0; b < 15; b++) g_gpt_ent[16 + b] = base[b];
    g_gpt_ent[16 + 15] = (u8)slot;
    wr32_(g_gpt_ent + 32, (u32)start_lba);
    wr32_(g_gpt_ent + 36, (u32)(start_lba >> 32));
    wr32_(g_gpt_ent + 40, (u32)end_lba);
    wr32_(g_gpt_ent + 44, (u32)(end_lba >> 32));
    /* UTF-16LE name "OCOS" */
    g_gpt_ent[56] = 'O'; g_gpt_ent[58] = 'C';
    g_gpt_ent[60] = 'O'; g_gpt_ent[62] = 'S';
    /* Both entry arrays (primary + backup). */
    u64 bents = backup_lba - ((u64)nent * esz + 511) / 512;
    if (gpt_write_entry(dev_idx, entries_lba, esz, slot) != 0) return 1;
    if (gpt_write_entry(dev_idx, bents, esz, slot) != 0) return 1;
    if (gpt_refresh_crcs(dev_idx, entries_lba, nent, esz, backup_lba) != 0)
        return 1;
    return 0;
}

int ocse_part_mkpart(const char* disk, u64 start_mib, u64 end_mib) {
    int dev_idx = ocse_part_disk_idx(disk);
    if (dev_idx < 0) {
        ocse_out(ocse_tr(OCSE_T_P_NODISK));
        ocse_outln(disk);
        return 1;
    }
    u64 start_lba, end_lba;
    if (mkpart_range(dev_idx, start_mib, end_mib, &start_lba, &end_lba) != 0)
        return 1;
    driver_block_part_table_t tbl;
    int have = driver_block_part_parse(dev_idx, &tbl) == 0;
    int rc;
    if (have && tbl.table_type == PART_TYPE_GPT)
        rc = gpt_mkpart(dev_idx, start_lba, end_lba);
    else if (have && tbl.table_type == PART_TYPE_MBR)
        rc = mbr_mkpart(dev_idx, start_lba, end_lba);
    else
        rc = mbr_mkpart(dev_idx, start_lba, end_lba);   /* fresh MBR */
    if (rc != 0) return rc;

    /* Register the new partition as a block device so mkfs/mount can
     * address it right away; then refresh the installer inventory.
     * The created slot is identified by its exact LBA range. */
    driver_block_part_table_t fresh;
    int created = -1;
    if (driver_block_part_parse(dev_idx, &fresh) == 0) {
        for (int i = 0; i < fresh.count; i++) {
            if (!fresh.parts[i].present) continue;
            if (fresh.parts[i].start_lba == start_lba &&
                fresh.parts[i].sectors == (u32)(end_lba - start_lba + 1)) {
                created = i + 1;
                break;
            }
        }
    }
    if (created > 0) {
        char cname[24];
        child_name(disk, created, cname, sizeof(cname));
        if (dev_is_mounted(cname)) {
            ocse_outln(ocse_tr(OCSE_T_P_MNT_WARN));
        } else {
            driver_block_part_register_child(disk, dev_idx, created,
                                             start_lba,
                                             end_lba - start_lba + 1);
        }
        ocse_out(ocse_tr(OCSE_T_P_CREATED));
        ocse_out(": ");
        ocse_outln(cname);
    } else {
        ocse_outln(ocse_tr(OCSE_T_P_CREATED));
    }
    ocse_disk_scan();
    return 0;
}

/* AUD-2e-001: lspart / "<disk>pN" device names / install all address
 * partitions by the DENSE number (parts[] index + 1 - the parsers skip
 * empty slots), but the on-disk write paths below need the RAW slot
 * (MBR 1-4, GPT entry index).  find_part_no keeps the dense number for
 * the outside world and hands the writers the raw slot via *slot_out;
 * it fails when dense #n names a hole or the raw slot is nonsense. */
static int find_part_no(int dev_idx, int part_no, int* is_gpt,
                        driver_block_part_table_t* tbl, int* slot_out) {
    if (driver_block_part_parse(dev_idx, tbl) != 0) return -1;
    *is_gpt = (tbl->table_type == PART_TYPE_GPT);
    if (part_no < 1 || part_no > tbl->count) return -1;
    if (!tbl->parts[part_no - 1].present) return -1;
    if (slot_out) {
        int slot = tbl->parts[part_no - 1].slot;
        if (*is_gpt) {
            if (slot < 0 || slot >= PART_MAX_PARTITIONS) return -1;
        } else {
            if (slot < 1 || slot > 4) return -1;
        }
        *slot_out = slot;
    }
    return 0;
}

int ocse_part_rmpart(const char* disk, int part_no) {
    int dev_idx = ocse_part_disk_idx(disk);
    if (dev_idx < 0) {
        ocse_out(ocse_tr(OCSE_T_P_NODISK));
        ocse_outln(disk);
        return 1;
    }
    char cname[24];
    child_name(disk, part_no, cname, sizeof(cname));
    if (dev_is_mounted(cname)) {
        ocse_outln(ocse_tr(OCSE_T_P_MNT_WARN));
        return 1;
    }
    int is_gpt, slot;
    driver_block_part_table_t tbl;
    if (find_part_no(dev_idx, part_no, &is_gpt, &tbl, &slot) != 0) {
        ocse_outln(ocse_tr(OCSE_T_P_BADPART));
        return 1;
    }
    if (is_gpt) {
        u64 entries_lba, backup_lba, last_usable;
        u32 nent, esz;
        if (gpt_read_hdr(dev_idx, &entries_lba, &nent, &esz,
                         &backup_lba, &last_usable) != 0) return 1;
        if (slot >= (int)nent) return 1;   /* defense: raw slot out of range */
        memset(g_gpt_ent, 0, 128);
        u64 bents = backup_lba - ((u64)nent * esz + 511) / 512;
        if (gpt_write_entry(dev_idx, entries_lba, esz, slot) != 0)
            return 1;
        if (gpt_write_entry(dev_idx, bents, esz, slot) != 0)
            return 1;
        if (gpt_refresh_crcs(dev_idx, entries_lba, nent, esz, backup_lba) != 0)
            return 1;
    } else {
        if (mbr_read(dev_idx) != 0) return 1;
        u8* p = g_sec + 446 + (slot - 1) * 16;   /* AUD-2e-001: raw slot */
        memset(p, 0, 16);
        if (mbr_write(dev_idx) != 0) return 1;
    }
    /* Unregister the partition child device. */
    int ci = driver_block_find_device(cname);
    if (ci >= 0) driver_block_unregister_device(ci);
    ocse_outln(ocse_tr(OCSE_T_P_REMOVED));
    ocse_disk_scan();
    return 0;
}

/* WP-06b: set the GPT partition type GUID (mkfs syncs the type with
 * the filesystem it just laid down - what mkfs on Linux leaves for
 * parted to see). Both entry arrays are updated and the CRCs
 * refreshed. Returns 0 on success. */
int ocse_part_gpt_set_type(const char* disk, int part_no,
                           const u8* guid) {
    int dev_idx = ocse_part_disk_idx(disk);
    if (dev_idx < 0 || part_no < 1) return 1;
    /* AUD-2e-001: part_no arrives as the DENSE lspart number (mkfs's
     * type sync parses it out of "<parent>pN"); resolve it to the raw
     * GPT entry slot before touching the on-disk array - with empty
     * slots present the two numbers differ and the old code patched
     * the wrong entry. */
    int is_gpt, slot;
    driver_block_part_table_t tbl;
    if (find_part_no(dev_idx, part_no, &is_gpt, &tbl, &slot) != 0)
        return 1;
    if (!is_gpt) return 1;
    u64 entries_lba, backup_lba, last_usable;
    u32 nent, esz;
    if (gpt_read_hdr(dev_idx, &entries_lba, &nent, &esz,
                     &backup_lba, &last_usable) != 0) return 1;
    if (slot >= (int)nent) return 1;       /* defense: raw slot out of range */
    if (gpt_read_entry(dev_idx, entries_lba, esz, slot) != 0)
        return 1;
    if (!g_gpt_ent[0] && !g_gpt_ent[1]) return 1;    /* unused slot */
    memcpy(g_gpt_ent, guid, 16);
    u64 bents = backup_lba - ((u64)nent * esz + 511) / 512;
    if (gpt_write_entry(dev_idx, entries_lba, esz, slot) != 0)
        return 1;
    if (gpt_write_entry(dev_idx, bents, esz, slot) != 0) return 1;
    return gpt_refresh_crcs(dev_idx, entries_lba, nent, esz, backup_lba);
}

int ocse_part_setflag(const char* disk, int part_no,
                      const char* flag, int value) {
    int dev_idx = ocse_part_disk_idx(disk);
    if (dev_idx < 0) {
        ocse_out(ocse_tr(OCSE_T_P_NODISK));
        ocse_outln(disk);
        return 1;
    }
    if (strcmp(flag, "boot") != 0) {
        ocse_outln(ocse_tr(OCSE_T_P_BADFLAG));
        return 1;
    }
    int is_gpt, slot;
    driver_block_part_table_t tbl;
    if (find_part_no(dev_idx, part_no, &is_gpt, &tbl, &slot) != 0) {
        ocse_outln(ocse_tr(OCSE_T_P_BADPART));
        return 1;
    }
    if (is_gpt) {
        u64 entries_lba, backup_lba, last_usable;
        u32 nent, esz;
        if (gpt_read_hdr(dev_idx, &entries_lba, &nent, &esz,
                         &backup_lba, &last_usable) != 0) return 1;
        if (slot >= (int)nent) return 1;   /* defense: raw slot out of range */
        if (gpt_read_entry(dev_idx, entries_lba, esz, slot) != 0)
            return 1;
        u64 attrs = rd32_(g_gpt_ent + 48) | ((u64)rd32_(g_gpt_ent + 52) << 32);
        if (value) attrs |= (1ULL << 2);        /* legacy BIOS bootable */
        else       attrs &= ~(1ULL << 2);
        wr32_(g_gpt_ent + 48, (u32)attrs);
        wr32_(g_gpt_ent + 52, (u32)(attrs >> 32));
        u64 bents = backup_lba - ((u64)nent * esz + 511) / 512;
        if (gpt_write_entry(dev_idx, entries_lba, esz, slot) != 0)
            return 1;
        if (gpt_write_entry(dev_idx, bents, esz, slot) != 0)
            return 1;
        if (gpt_refresh_crcs(dev_idx, entries_lba, nent, esz, backup_lba) != 0)
            return 1;
    } else {
        if (mbr_read(dev_idx) != 0) return 1;
        u8* p = g_sec + 446 + (slot - 1) * 16;   /* AUD-2e-001: raw slot */
        p[0] = value ? 0x80 : 0x00;
        if (mbr_write(dev_idx) != 0) return 1;
    }
    ocse_outln(ocse_tr(OCSE_T_P_FLAGSET));
    ocse_disk_scan();
    return 0;
}
