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
