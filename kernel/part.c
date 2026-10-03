/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/part.c
 * Purpose: MBR + GPT partition table parser.
 */
#include "part.h"
#include "blk.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "log.h"

#pragma pack(push, 1)
typedef struct {
    u8  boot;
    u8  start_chs[3];
    u8  type;
    u8  end_chs[3];
    u32 start_lba;
    u32 sectors;
} mbr_entry_t;

typedef struct {
    u8  boot_code[446];
    mbr_entry_t parts[4];
    u16 signature;   /* 0xAA55 */
} mbr_sector_t;

typedef struct {
    char sig[8];           /* "EFI PART" */
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;
    u64 alt_lba;
    u64 first_usable;
    u64 last_usable;
    u8  disk_guid[16];
    u64 part_entry_start;
    u32 num_entries;
    u32 entry_size;
    u32 entries_crc32;
} gpt_header_t;

typedef struct {
    u8  type_guid[16];
    u8  unique_guid[16];
    u64 first_lba;
    u64 last_lba;
    u64 attributes;
    char name[72];   /* UTF-16LE, but we store as raw bytes */
} gpt_entry_t;
#pragma pack(pop)

int part_parse_mbr(int dev_idx, part_table_t *out) {
    if (!out) return -1;
    oc_memset(out, 0, sizeof(*out));
    out->dev_idx = dev_idx;
    out->table_type = PART_TYPE_MBR;

    u8 buf[512];
    if (blk_read_sectors_raw(dev_idx, 0, 1, buf) != 0) return -1;

    mbr_sector_t *mbr = (mbr_sector_t *)buf;
    if (mbr->signature != 0xAA55) return -1;

    for (int i = 0; i < 4; i++) {
        mbr_entry_t *e = &mbr->parts[i];
        if (e->type == 0) continue;
        if (out->count >= PART_MAX_PARTITIONS) break;
        /* BUG-017 FIX: Validate partition entry before accepting it.
         * Old code accepted any partition entry from sector 0, even if
         * the "MBR" was actually a superfloppy (FAT boot sector with
         * 0x55AA signature). This caused bogus partitions with start=0
         * and sector count exceeding disk capacity.
         * Now we check: start_lba > 0 and start_lba + sectors <= capacity. */
        if (e->start_lba == 0) continue;  /* start LBA 0 = bogus */
        /* Get device capacity to validate */
        blk_device_t *dev = blk_get_device(dev_idx);
        if (dev) {
            u64 end_lba = (u64)e->start_lba + e->sectors;
            if (end_lba > dev->sectors) continue;  /* exceeds disk */
        }
        partition_t *p = &out->parts[out->count];
        p->present = 1;
        p->bootable = (e->boot & 0x80) ? 1 : 0;
        p->type = e->type;
        p->start_lba = e->start_lba;
        p->sectors = e->sectors;
        char nm[20]; char n[8];
        oc_strcpy(nm, "MBR-"); oc_u64_to_str(out->count, n); oc_strcat(nm, n);
        oc_strncpy(p->name, nm, PART_NAME_LEN - 1);
        out->count++;
    }

    /* Check for GPT protective partition (type 0xEE). */
    for (int i = 0; i < out->count; i++) {
        if (out->parts[i].type == 0xEE) {
            /* This is actually a GPT disk — reparse as GPT. */
            return part_parse_gpt(dev_idx, out);
        }
    }
    return 0;
}

int part_parse_gpt(int dev_idx, part_table_t *out) {
    if (!out) return -1;
    oc_memset(out, 0, sizeof(*out));
    out->dev_idx = dev_idx;
    out->table_type = PART_TYPE_GPT;

    u8 buf[512];
    /* Read GPT header at LBA 1. */
    if (blk_read_sectors_raw(dev_idx, 1, 1, buf) != 0) return -1;
    gpt_header_t *gh = (gpt_header_t *)buf;
    if (oc_memcmp(gh->sig, "EFI PART", 8) != 0) return -1;

    u32 num_entries = gh->num_entries;
    u32 entry_size = gh->entry_size;
    u64 entry_start = gh->part_entry_start;
    if (num_entries > PART_MAX_PARTITIONS) num_entries = PART_MAX_PARTITIONS;
    if (entry_size < sizeof(gpt_entry_t)) entry_size = sizeof(gpt_entry_t);

    /* Read partition entries. They usually start at LBA 2 and may span
     * multiple sectors. Read up to 8 sectors (enough for 128 entries). */
    u8 entry_buf[512 * 8];
    u32 sectors_to_read = (num_entries * entry_size + 511) / 512;
    if (sectors_to_read > 8) sectors_to_read = 8;
    if (blk_read_sectors_raw(dev_idx, entry_start, sectors_to_read, entry_buf) != 0) return -1;

    for (u32 i = 0; i < num_entries; i++) {
        gpt_entry_t *e = (gpt_entry_t *)(entry_buf + (u64)i * entry_size);
        /* Check if entry is used (type GUID != all zeros). */
        int used = 0;
        for (int j = 0; j < 16; j++) {
            if (e->type_guid[j] != 0) { used = 1; break; }
        }
        if (!used) continue;
        if (out->count >= PART_MAX_PARTITIONS) break;
        partition_t *p = &out->parts[out->count];
        p->present = 1;
        p->type = e->type_guid[0];   /* first byte is a rough type indicator */
        p->start_lba = e->first_lba;
        p->sectors = e->last_lba - e->first_lba + 1;
        oc_memcpy(p->gpt_type_guid, e->type_guid, 16);
        /* Extract ASCII name from UTF-16LE (take low bytes, stop at null). */
        int ni = 0;
        for (int j = 0; j < 72 && ni < PART_NAME_LEN - 1; j += 2) {
            char c = e->name[j];
            if (c == 0) break;
            p->name[ni++] = c;
        }
        p->name[ni] = 0;
        if (ni == 0) {
            char nm[20]; char n[8];
            oc_strcpy(nm, "GPT-"); oc_u64_to_str(out->count, n); oc_strcat(nm, n);
            oc_strncpy(p->name, nm, PART_NAME_LEN - 1);
        }
        out->count++;
    }
    return 0;
}

int part_parse(int dev_idx, part_table_t *out) {
    /* Try MBR first — it will auto-detect GPT if the protective entry is found. */
    return part_parse_mbr(dev_idx, out);
}

partition_t *part_get_partition(part_table_t *tbl, int index) {
    if (!tbl || index < 0 || index >= tbl->count) return NULL;
    return &tbl->parts[index];
}

/* ---- WP-10d-pre: partition block-device registration -------------
 *
 * Extracted from ab_update.c (WP-10u) so the in-system abdisk /
 * install commands can register freshly written partition tables
 * without rebooting.  Behavior is identical to the original
 * ab_register_partition(): child device named "<parent>pN" that
 * forwards sector I/O to the parent with a start-LBA offset.
 */
typedef struct {
    int parent_idx;
    u64 start_lba;
} part_child_priv_t;

static int part_child_read(blk_device_t *dev, u64 lba, u32 count, void *buf) {
    part_child_priv_t *p = (part_child_priv_t *)dev->priv;
    if (!p) return -1;
    return blk_read_sectors_raw(p->parent_idx, p->start_lba + lba, count, buf);
}

static int part_child_write(blk_device_t *dev, u64 lba, u32 count, const void *buf) {
    part_child_priv_t *p = (part_child_priv_t *)dev->priv;
    if (!p) return -1;
    return blk_write_sectors_raw(p->parent_idx, p->start_lba + lba, count, buf);
}

static const blk_ops_t g_part_child_ops = {
    .read  = part_child_read,
    .write = part_child_write,
    .flush = NULL,               /* parent driver flush runs separately */
};

int part_register_child(const char *parent_name, int parent_idx,
                        int part_no, u64 start_lba, u64 sectors) {
    if (!parent_name || parent_idx < 0 || part_no < 1 || part_no > 99)
        return -1;
    blk_device_t *parent = blk_get_device(parent_idx);
    if (!parent || !parent->present) return -1;

    char name[BLK_DEV_NAME_LEN];
    oc_strncpy(name, parent_name, sizeof(name) - 8);
    name[sizeof(name) - 8] = 0;
    int len = (int)oc_strlen(name);
    name[len++] = 'p';
    char num[4];
    oc_u64_to_str((u64)part_no, num);
    oc_strcpy(name + len, num);

    int existing = blk_find_device(name);
    if (existing >= 0) return existing;

    part_child_priv_t *priv =
        (part_child_priv_t *)kmalloc(sizeof(part_child_priv_t));
    if (!priv) return -1;
    priv->parent_idx = parent_idx;
    priv->start_lba = start_lba;

    blk_device_t dev;
    oc_memset(&dev, 0, sizeof(dev));
    oc_strncpy(dev.name, name, BLK_DEV_NAME_LEN - 1);
    dev.type = parent->type;          /* partition inherits the disk class */
    dev.sectors = sectors;
    dev.sector_size = 512;
    dev.present = 1;
    dev.priv = priv;
    int idx = blk_register(&dev, &g_part_child_ops);
    if (idx < 0) kfree(priv);
    return idx;
}

/* ---- WP-10d-pre: MBR partition table writer ------------------------
 * Used by abdisk / install to lay out a disk from inside the OS.
 * Sector 0 = 446 bytes code area (left zero here; grub-install fills
 * it with the GRUB boot image), 4 x 16-byte entries, 0xAA55 signature.
 */
int part_write_mbr_table(int dev_idx,
                         const u8 types[4], const u32 starts[4],
                         const u32 sectors4[4]) {
    if (dev_idx < 0 || !types || !starts || !sectors4) return -1;
    u8 buf[512];
    oc_memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 4; i++) {
        if (types[i] == 0) continue;
        u8 *e = buf + 446 + i * 16;
        e[0] = 0x00;                       /* not bootable (GRUB manages) */
        e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
        e[4] = types[i];
        e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
        u32 start = starts[i], size = sectors4[i];
        oc_memcpy(e + 8, &start, 4);       /* little-endian host */
        oc_memcpy(e + 12, &size, 4);
    }
    buf[510] = 0x55;
    buf[511] = 0xAA;
    return blk_write_sectors_raw(dev_idx, 0, 1, buf);
}

/* ---- WP-10d-pre: boot-time partition scan --------------------------
 * Register "<parent>pN" child devices for every partition of every
 * block device (MBR or GPT).  Without this, a single-system install
 * disk (1 partition) has no child devices after a reboot and its
 * system partition cannot be mounted; the A/B scan in ab_update.c only
 * covers the A/B layout.  Idempotent via part_register_child().
 */
void part_scan_register_all(void) {
    int n = blk_num_devices();      /* snapshot: children registered
                                       below are not re-scanned */
    int registered = 0;
    for (int i = 0; i < n; i++) {
        blk_device_t *dev = blk_get_device(i);
        if (!dev || !dev->present) continue;
        part_table_t tbl;
        if (part_parse(i, &tbl) != 0) continue;
        for (int p = 0; p < tbl.count && p < PART_MAX_PARTITIONS; p++) {
            if (!tbl.parts[p].present) continue;
            if (part_register_child(dev->name, i, p + 1,
                                    tbl.parts[p].start_lba,
                                    tbl.parts[p].sectors) >= 0)
                registered++;
        }
    }
    if (registered > 0) {
        char line[80]; char num[8];
        oc_strcpy(line, "partscan: ");
        oc_u64_to_str((u64)registered, num); oc_strcat(line, num);
        oc_strcat(line, " partition device(s) registered");
        oc_log_info(line);
    }
}

void part_print(const part_table_t *tbl) {
    if (!tbl) return;
    char line[120]; char n[24];
    const char *tn = (tbl->table_type == PART_TYPE_GPT) ? "GPT" : "MBR";
    oc_strcpy(line, "Partition table ("); oc_strcat(line, tn); oc_strcat(line, "):\n");
    oc_console_puts(line);
    oc_console_puts("  #  Start          Sectors        Type  Name\n");
    for (int i = 0; i < tbl->count; i++) {
        const partition_t *p = &tbl->parts[i];
        if (!p->present) continue;
        oc_strcpy(line, "  "); oc_u64_to_str(i, n); oc_strcat(line, n);
        oc_strcat(line, "  ");
        oc_u64_to_str(p->start_lba, n); int pad = 14 - oc_strlen(n);
        for (int j = 0; j < pad; j++) oc_strcat(line, " ");
        oc_strcat(line, n); oc_strcat(line, "  ");
        oc_u64_to_str(p->sectors, n); pad = 14 - oc_strlen(n);
        for (int j = 0; j < pad; j++) oc_strcat(line, " ");
        oc_strcat(line, n); oc_strcat(line, "  0x");
        oc_u64_to_hex(p->type, n, 2); oc_strcat(line, n);
        oc_strcat(line, "  "); oc_strcat(line, p->name);
        oc_strcat(line, "\n");
        oc_console_puts(line);
    }
}
