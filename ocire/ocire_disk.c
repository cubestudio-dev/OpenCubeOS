/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_disk.c
 * Purpose: recovery-disk discovery + mailbox (see ocire_disk.h).
 */

#include "ocire_disk.h"
#include "ocire_util.h"
#include "ocire_sha256.h"

static ocire_layout_t g_layout;
static ocire_golden_t g_golden;
static int g_golden_loaded;

const ocire_layout_t *ocire_layout(void) {
    return (const ocire_layout_t *)&g_layout;
}

#pragma pack(push, 1)
typedef struct {
    u8  boot;
    u8  start_chs[3];
    u8  type;
    u8  end_chs[3];
    u32 start_lba;
    u32 sectors;
} part_ent_t;

typedef struct {
    u8  code[446];
    part_ent_t parts[4];
    u16 sig;
} mbr_t;
#pragma pack(pop)

/* Try to mount the 0xEE shadow volume on `ata_idx`. */
static int try_ocire_disk(int ata_idx, u64 *part_lba_out) {
    u8 sec[512];
    if (ocire_ata_read(ata_idx, 0, 1, sec) != 0) return -1;
    mbr_t *mbr = (mbr_t *)sec;
    if (mbr->sig != 0xAA55) return -1;
    for (int i = 0; i < 4; i++) {
        if (mbr->parts[i].type != 0xEE) continue;
        u64 start = mbr->parts[i].start_lba;
        if (start == 0) return -1;
        /* No GPT header behind the shadow entry (WP-01 design). */
        u8 lba1[512];
        if (ocire_ata_read(ata_idx, 1, 1, lba1) != 0) return -1;
        if (oc_memcmp(lba1, "EFI PART", 8) == 0) return -1;
        /* FAT12 volume at the shadow start. */
        ocire_fat_vol_t v;
        if (ocire_fat_mount(&v, ata_idx, start) == 0 && v.fat_type == 12) {
            *part_lba_out = start;
            return 0;
        }
        return -1;
    }
    return -1;
}

/* Try to mount the OCOS FAT32 volume on `ata_idx`. */
static int try_ocos_disk(int ata_idx, u64 *part_lba_out, u64 *part_secs_out) {
    u8 sec[512];
    if (ocire_ata_read(ata_idx, 0, 1, sec) != 0) return -1;
    mbr_t *mbr = (mbr_t *)sec;
    if (mbr->sig != 0xAA55) return -1;
    for (int i = 0; i < 4; i++) {
        if (mbr->parts[i].type == 0 || mbr->parts[i].type == 0xEE) continue;
        u64 start = mbr->parts[i].start_lba;
        u64 sectors = mbr->parts[i].sectors;
        if (start == 0 || sectors < 2048) continue;
        ocire_fat_vol_t v;
        if (ocire_fat_mount(&v, ata_idx, start) == 0 && v.fat_type == 32) {
            /* It is the OCOS volume when the kernel lives there. */
            ocire_fat_file_t f;
            if (ocire_fat_lookup(&v, "BOOT/OPENCUBE.ELF", &f) == 0) {
                *part_lba_out = start;
                *part_secs_out = sectors;
                return 0;
            }
        }
    }
    return -1;
}

int ocire_disk_scan(void) {
    oc_memset(&g_layout, 0, sizeof(g_layout));
    g_layout.ocos_idx = -1;
    g_layout.ocire_idx = -1;
    g_golden_loaded = 0;

    int n = ocire_ata_scan();
    for (int i = 0; i < n; i++) {
        const ocire_ata_dev_t *d = ocire_ata_dev(i);
        if (!d->present) continue;
        if (g_layout.ocire_idx < 0) {
            u64 plba;
            if (try_ocire_disk(i, &plba) == 0) {
                g_layout.ocire_idx = i;
                g_layout.ocire_part_lba = plba;
                /* mount kept for later use */
                ocire_fat_mount(&g_layout.ocire_vol, i, plba);
            }
        }
        if (g_layout.ocos_idx < 0) {
            u64 plba, psecs;
            if (try_ocos_disk(i, &plba, &psecs) == 0) {
                g_layout.ocos_idx = i;
                g_layout.ocos_part_lba = plba;
                g_layout.ocos_part_sectors = psecs;
                ocire_fat_mount(&g_layout.ocos_vol, i, plba);
            }
        }
    }

    if (g_layout.ocire_idx < 0) return -1;
    if (g_layout.ocos_idx < 0) return -2;

    /* Mailbox + log must be locatable on the recovery volume. */
    if (ocire_fat_lookup(&g_layout.ocire_vol, "MAILBOX.BIN",
                         &g_layout.mb_file) != 0 ||
        ocire_fat_lookup(&g_layout.ocire_vol, "LOG/BOOT.LOG",
                         &g_layout.log_file) != 0) {
        return -3;
    }
    if (ocire_mailbox_read() != 0) return -3;
    return 0;
}

int ocire_mailbox_read(void) {
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&g_layout.ocire_vol, "MAILBOX.BIN", &f) != 0)
        return -1;
    s64 got = ocire_fat_read_file(&g_layout.ocire_vol, &f,
                                  (u8 *)&g_layout.mb, sizeof(g_layout.mb));
    if (got < (s64)OCIRE_MAILBOX_BYTES) {
        /* Missing or short: treat as an uninitialized mailbox. */
        oc_memset(&g_layout.mb, 0, sizeof(g_layout.mb));
        return -2;
    }
    if (oc_memcmp(g_layout.mb.magic, OCIRE_MAILBOX_MAGIC, 8) != 0) {
        oc_memset(&g_layout.mb, 0, sizeof(g_layout.mb));
        return -2;
    }
    return 0;
}

int ocire_mailbox_write(void) {
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&g_layout.ocire_vol, "MAILBOX.BIN", &f) != 0)
        return -1;
    if (ocire_fat_file_patch(&g_layout.ocire_vol, &f, 0,
                             &g_layout.mb, OCIRE_MAILBOX_BYTES) != 0)
        return -2;
    return 0;
}

int ocire_golden_load(ocire_golden_t *g) {
    if (g_golden_loaded) {
        *g = g_golden;
        return 0;
    }
    oc_memset(&g_golden, 0, sizeof(g_golden));
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&g_layout.ocire_vol, "GOLDEN/MANIFEST.TXT", &f) != 0)
        return -1;
    static char buf[2048];
    s64 got = ocire_fat_read_file(&g_layout.ocire_vol, &f,
                                  (u8 *)buf, sizeof(buf) - 1);
    if (got <= 0) return -2;
    buf[got] = 0;

    /* Lines: "SHA256HEX SIZE PATH". */
    char *p = buf;
    while (*p && g_golden.n < OCIRE_GOLDEN_MAX) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int eol = (*p == '\n');
        *p = 0;
        if (eol) p++;
        /* strip CR */
        int L = (int)oc_strlen(line);
        if (L > 0 && line[L - 1] == '\r') line[L - 1] = 0;
        if (line[0] == 0) continue;

        ocire_golden_ent_t *e = &g_golden.e[g_golden.n];
        oc_memset(e, 0, sizeof(*e));
        /* 64 hex chars */
        int i = 0;
        for (; i < 64 && line[i]; i++) e->sha_hex[i] = line[i];
        e->sha_hex[i] = 0;
        if (i != 64 || line[64] != ' ') continue;
        char *q = line + 65;
        u64 size = 0;
        for (; *q >= '0' && *q <= '9'; q++) {
            size = size * 10 + (u64)(*q - '0');
        }
        if (*q != ' ') continue;
        q++;
        int n = 0;
        for (; *q && n < 31; q++, n++) e->path[n] = *q;
        e->path[n] = 0;
        if (n == 0) continue;
        e->size = size;
        g_golden.n++;
    }
    if (g_golden.n == 0) return -3;
    g_golden_loaded = 1;
    *g = g_golden;
    return 0;
}

s64 ocire_golden_read(const char *path, u8 *buf, u64 cap) {
    /* Golden files live at /GOLDEN/<base> on the recovery volume. */
    char full[48];
    int i = 0;
    const char *g = "GOLDEN/";
    for (; *g && i < 40; g++) full[i++] = *g;
    const char *p = path;
    /* path is OCOS-relative ("BOOT/OPENCUBE.ELF"): keep as-is. */
    for (; *p && i < 46; p++) full[i++] = *p;
    full[i] = 0;
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&g_layout.ocire_vol, full, &f) != 0) return -1;
    return ocire_fat_read_file(&g_layout.ocire_vol, &f, buf, cap);
}
