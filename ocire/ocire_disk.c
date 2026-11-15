/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_disk.c
 * Purpose: recovery-disk discovery + mailbox (see ocire_disk.h).
 */

#include "ocire_disk.h"
#include "ocire_util.h"
#include "ocire_console.h"
#include "ocire_sha256.h"

static ocire_layout_t g_layout;
static ocire_golden_t g_golden;
static int g_golden_loaded;

const ocire_layout_t *ocire_layout(void) {
    return (const ocire_layout_t *)&g_layout;
}

ocire_layout_t *ocire_layout_mut(void) {
    return &g_layout;
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

typedef struct {
    char sig[8];          /* "EFI PART"                        */
    u32 rev;
    u32 hdr_size;
    u32 crc;
    u32 rsvd;
    u64 cur_lba;
    u64 bak_lba;
    u64 first_usable;
    u64 last_usable;
    u8  disk_guid[16];
    u64 part_lba;
    u32 n_parts;
    u32 part_size;
    u32 crc_parts;
    u8  pad[420];
} gpt_hdr_t;

typedef struct {
    u8  type_guid[16];
    u8  part_guid[16];
    u64 first_lba;
    u64 last_lba;
    u64 attrs;
    u16 name[36];
} gpt_ent_t;
#pragma pack(pop)

/* the GPT EFI System Partition type GUID (C12A7328-F81F-11D2-...)
 * in the on-disk mixed-endian byte order */
static const u8 GPT_ESP_GUID[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

/* Try to mount the legacy 0xEE FAT12 shadow volume on `ata_idx`. */
static int try_ocire_legacy(int ata_idx, u64 *part_lba_out) {
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

/* Does the FAT32 volume at (ata_idx, start) look like an OCIRE
 * recovery SLOT (carries the boot kernel + the mailbox)? */
static int is_ocire_slot(int ata_idx, u64 start, u64 sectors) {
    ocire_fat_vol_t v;
    if (start == 0 || sectors < 2048) return 0;
    if (ocire_fat_mount(&v, ata_idx, start) != 0 || v.fat_type != 32)
        return 0;
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&v, "BOOT/OCIRE.ELF", &f) != 0) return 0;
    if (ocire_fat_lookup(&v, "MAILBOX.BIN", &f) != 0) return 0;
    return 1;
}

/* The WP-07 recovery disk: MBR with TWO FAT32 OCIRE slots (p1=A,
 * p2=B) and the OCIREAB1 flags sector at LBA 1024.  Fills the slot
 * map + flags in g_layout and mounts the flags-selected slot.
 * Returns 0 when a dual-slot recovery disk was recognized. */
static int try_ocire_dual_slot(int ata_idx) {
    u8 sec[512];
    if (ocire_ata_read(ata_idx, 0, 1, sec) != 0) return -1;
    mbr_t *mbr = (mbr_t *)sec;
    if (mbr->sig != 0xAA55) return -1;

    u64 lba[3] = {0, 0, 0};
    u64 secs[3] = {0, 0, 0};
    for (int i = 0; i < 4 && (i + 1) <= 2; i++) {
        u64 start = mbr->parts[i].start_lba;
        u64 n = mbr->parts[i].sectors;
        if (is_ocire_slot(ata_idx, start, n)) {
            lba[i + 1] = start;
            secs[i + 1] = n;
        }
    }
    if (lba[1] == 0 || lba[2] == 0)
        return -1;              /* not the dual-slot recovery disk */

    g_layout.ocire_idx = ata_idx;
    g_layout.dual_slot = 1;
    g_layout.slot_lba[1] = lba[1];
    g_layout.slot_lba[2] = lba[2];
    g_layout.slot_sectors[1] = secs[1];
    g_layout.slot_sectors[2] = secs[2];

    /* the flags sector decides which slot is live */
    ocire_ab_read(&g_layout);
    int pick;
    if (g_layout.ab_valid) {
        int t = g_layout.ab.try_slot;
        if (t == 1 || t == 2)
            pick = t;
        else if (g_layout.ab.active_slot == 1 || g_layout.ab.active_slot == 2)
            pick = g_layout.ab.active_slot;
        else
            pick = 1;
    } else {
        pick = 1;
    }
    g_layout.my_slot = pick;
    g_layout.ocire_part_lba = g_layout.slot_lba[pick];
    if (ocire_fat_mount(&g_layout.ocire_vol, ata_idx,
                        g_layout.ocire_part_lba) != 0)
        return -1;
    return 0;
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

/* ---- WP-07: ESP discovery (GPT first, MBR types second) --------- */

/* Mount the FAT32 volume at start as the ESP when it carries /EFI. */
static int try_esp_volume(int ata_idx, u64 start, u64 sectors) {
    if (start == 0 || sectors < 1024) return -1;
    ocire_fat_vol_t v;
    if (ocire_fat_mount(&v, ata_idx, start) != 0 || v.fat_type != 32)
        return -1;
    /* an ESP always has the \EFI directory; an EFI/BOOT tree is the
     * mainstream removable-media shape, our own drop zone is
     * EFI/OPENCUBE - either proves ESP-ness */
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&v, "EFI", &f) != 0) return -1;
    g_layout.esp_idx = ata_idx;
    g_layout.esp_part_lba = start;
    g_layout.esp_vol = v;
    return 0;
}

static int try_esp_gpt(int ata_idx) {
    u8 sec[512];
    if (ocire_ata_read(ata_idx, 1, 1, sec) != 0) return -1;
    gpt_hdr_t *h = (gpt_hdr_t *)sec;
    if (oc_memcmp(h->sig, "EFI PART", 8) != 0) return -1;
    if (h->n_parts == 0 || h->part_size < 128 || h->part_size > 512)
        return -1;
    ocire_puts("[esp] GPT disk found on ata");
    ocire_putdec("", (u64)ata_idx);
    ocire_puts(" - scanning partition entries\n");
    u32 n = h->n_parts > 32 ? 32 : h->n_parts;
    u8 ents[512];
    u32 per_sec = 512 / h->part_size;
    for (u32 i = 0; i < n; i++) {
        u64 lba = h->part_lba + (u64)i / per_sec;
        u32 off = ((u32)i % per_sec) * h->part_size;
        if (off == 0 || (i % per_sec) == 0) {
            if (ocire_ata_read(ata_idx, lba, 1, ents) != 0) return -1;
        }
        gpt_ent_t *e = (gpt_ent_t *)(ents + off);
        /* all-zero type GUID = unused entry */
        int used = 0;
        for (int k = 0; k < 16; k++)
            if (e->type_guid[k]) { used = 1; break; }
        if (!used) continue;
        if (oc_memcmp(e->type_guid, GPT_ESP_GUID, 16) == 0) {
            return try_esp_volume(ata_idx, e->first_lba,
                                  e->last_lba - e->first_lba + 1);
        }
    }
    return -1;
}

static int try_esp_mbr(int ata_idx) {
    u8 sec[512];
    if (ocire_ata_read(ata_idx, 0, 1, sec) != 0) return -1;
    mbr_t *mbr = (mbr_t *)sec;
    if (mbr->sig != 0xAA55) return -1;
    for (int i = 0; i < 4; i++) {
        u8 t = mbr->parts[i].type;
        if (t != 0xEF && t != 0x0C && t != 0x0B) continue;
        u64 start = mbr->parts[i].start_lba;
        u64 sectors = mbr->parts[i].sectors;
        if (start == 0) continue;
        if (try_esp_volume(ata_idx, start, sectors) == 0) return 0;
    }
    return -1;
}

/* ---- WP-07: the A/B flags sector --------------------------------- */

static u32 ab_checksum(const ocire_abflags_t *f) {
    /* ones-complement sum over the first 16 payload bytes (u16 words
     * 0..7; the cksum word pair at 8/9 is skipped - #52 discipline,
     * mirrored by the OCBL writer and make_ocire_disk.sh) */
    const u16 *w = (const u16 *)f;
    u32 sum = 0;
    for (u32 i = 0; i < 8; i++)
        sum += (u32)w[i];
    return (~sum) & 0xFFFFu;
}

int ocire_ab_read(ocire_layout_t *L) {
    u8 sec[512];
    oc_memset(&L->ab, 0, sizeof(L->ab));
    L->ab_valid = 0;
    if (L->ocire_idx < 0) return -1;
    if (ocire_ata_read(L->ocire_idx, OCIRE_AB_FLAGS_LBA, 1, sec) != 0)
        return -2;
    ocire_abflags_t *f = (ocire_abflags_t *)sec;
    if (oc_memcmp(f->magic, OCIRE_AB_MAGIC, 8) != 0) return -3;
    if (f->cksum != ab_checksum(f)) return -4;
    if ((f->active_slot | f->try_slot) != 0 &&
        f->active_slot > 2 && f->try_slot > 2) return -5;
    L->ab = *f;
    L->ab_valid = 1;
    return 0;
}

int ocire_ab_write(ocire_layout_t *L) {
    if (L->ocire_idx < 0) return -1;
    u8 sec[512];
    oc_memset(sec, 0, sizeof(sec));
    ocire_abflags_t *f = (ocire_abflags_t *)sec;
    *f = L->ab;
    oc_memcpy(f->magic, OCIRE_AB_MAGIC, 8);
    f->cksum = ab_checksum(f);
    if (ocire_ata_write(L->ocire_idx, OCIRE_AB_FLAGS_LBA, 1, sec) != 0)
        return -2;
    return 0;
}

int ocire_ab_confirm_boot(ocire_layout_t *L) {
    if (!L->dual_slot) return 1;
    if (ocire_ab_read(L) != 0) return -1;
    int my = L->my_slot;
    if (!L->ab_valid) {
        /* fresh/uninitialized flags: write the sane default */
        oc_memset(&L->ab, 0, sizeof(L->ab));
        L->ab.active_slot = (u8)(my ? my : 1);
        L->ab.try_slot = 0;
        L->ab.boot_count = 1;
        L->ab_valid = 1;
        return ocire_ab_write(L);
    }
    if (L->ab.try_slot == my) {
        L->ab.active_slot = (u8)my;      /* promote the candidate */
        L->ab.try_slot = 0;
        L->ab.attempts = 0;
        if (L->ab.boot_count < 0xFFFF) L->ab.boot_count++;
        return ocire_ab_write(L);
    }
    if (L->ab.active_slot != my) {
        /* booted from a slot the flags do not track (GRUB menu
         * path): follow reality */
        L->ab.active_slot = (u8)my;
        L->ab.try_slot = 0;
        L->ab.attempts = 0;
        if (L->ab.boot_count < 0xFFFF) L->ab.boot_count++;
        return ocire_ab_write(L);
    }
    if (L->ab.boot_count < 0xFFFF) L->ab.boot_count++;
    return ocire_ab_write(L);
}

int ocire_ab_other_slot(const ocire_layout_t *L) {
    if (!L->dual_slot) return 0;
    if (L->my_slot == 1) return 2;
    if (L->my_slot == 2) return 1;
    return 0;
}

int ocire_slot_mount(ocire_layout_t *L, int slot, ocire_fat_vol_t *vol) {
    if (!L->dual_slot || L->ocire_idx < 0) return -1;
    if (slot != 1 && slot != 2) return -1;
    if (L->slot_lba[slot] == 0) return -2;
    if (ocire_fat_mount(vol, L->ocire_idx, L->slot_lba[slot]) != 0)
        return -3;
    return 0;
}

int ocire_disk_scan(void) {
    oc_memset(&g_layout, 0, sizeof(g_layout));
    g_layout.ocos_idx = -1;
    g_layout.ocire_idx = -1;
    g_layout.esp_idx = -1;
    g_golden_loaded = 0;

    int n = ocire_ata_scan();
    for (int i = 0; i < n; i++) {
        const ocire_ata_dev_t *d = ocire_ata_dev(i);
        if (!d->present) continue;
        if (g_layout.ocire_idx < 0) {
            /* WP-07 dual-slot recovery disk first, legacy FAT12
             * shadow second (old media keeps working) */
            if (try_ocire_dual_slot(i) == 0)
                continue;               /* found + mounted */
            u64 plba;
            if (try_ocire_legacy(i, &plba) == 0) {
                g_layout.ocire_idx = i;
                g_layout.ocire_part_lba = plba;
                g_layout.my_slot = 1;   /* legacy = slot A of one */
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
        if (g_layout.esp_idx < 0) {
            /* the ESP never lives on the recovery disk; the OCOS
             * GRUB disk (hd0, MBR 0x0C p1) would false-positive as
             * an ESP - only accept GPT ESPs and MBR 0xEF/0x0C/0x0B
             * on OTHER disks without the OCOS kernel */
            if (g_layout.ocire_idx != i) {
                if (try_esp_gpt(i) != 0)
                    try_esp_mbr(i);
                /* reject an "ESP" that is really the OCOS volume */
                if (g_layout.esp_idx == i && g_layout.ocos_idx == i) {
                    g_layout.esp_idx = -1;
                    g_layout.esp_part_lba = 0;
                    oc_memset(&g_layout.esp_vol, 0, sizeof(g_layout.esp_vol));
                }
            }
        }
    }

    if (g_layout.ocire_idx < 0) return -1;

    /* Locate the recovery-volume files BEFORE the system-volume
     * verdict: repair full must be able to work with an UNMOUNTABLE
     * system volume (that is its job) and still needs the mailbox,
     * the log ring and the agent digest facts.  Found live by the
     * WP-02 full-repair battery: the early -2 used to leave
     * mb_file/log_file zeroed and the rebuilt mailbox carried no
     * log LBA and no agent digest. */
    int files_ok =
        (ocire_fat_lookup(&g_layout.ocire_vol, "MAILBOX.BIN",
                          &g_layout.mb_file) == 0 &&
         ocire_fat_lookup(&g_layout.ocire_vol, "LOG/BOOT.LOG",
                          &g_layout.log_file) == 0 &&
         ocire_mailbox_read() == 0);
    if (!files_ok) return -3;
    if (g_layout.ocos_idx < 0) return -2;
    return 0;
}

/* #52 (WP-fix-p2-p3-cleanup): ones-complement checksum over the
 * mailbox as u16 words, skipping the cksum field itself (bytes
 * 64..67 = words 32/33).  Mirrored byte-for-byte by
 * tools/make_ocire_disk.sh so a fresh disk and the kernel agree. */
static u32 ocire_mb_checksum(const ocire_mailbox_t *mb) {
    const u16 *w = (const u16 *)mb;
    u32 sum = 0;
    for (u32 i = 0; i < OCIRE_MAILBOX_BYTES / 2; i++) {
        if (i == 32 || i == 33) continue;   /* the cksum word pair */
        sum += (u32)w[i];
        if (sum & 0xFFFF0000u) sum = (sum & 0xFFFFu) + 1u;
    }
    return (~sum) & 0xFFFFu;
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
    /* #52: the old read only compared the magic - a power-cut torn
     * patch left half-new/half-old fields that still passed, and a
     * garbage state value could steer the supervisor down the
     * "normal handover" branch.  Reject illegal state enums on every
     * mailbox, and enforce the checksum on ver>=1 (ver=0 = legacy
     * disk written before this fix; its torn writes are not
     * detectable, but every kernel write from now on stamps ver=1). */
    if (g_layout.mb.state > OCIRE_MB_STATE_ENTER_FULL) {
        oc_memset(&g_layout.mb, 0, sizeof(g_layout.mb));
        return -2;
    }
    if (g_layout.mb.ver >= 1) {
        if (g_layout.mb.ver > 1 ||
            g_layout.mb.cksum != ocire_mb_checksum(&g_layout.mb)) {
            oc_memset(&g_layout.mb, 0, sizeof(g_layout.mb));
            return -2;
        }
    }
    return 0;
}

int ocire_mailbox_write(void) {
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&g_layout.ocire_vol, "MAILBOX.BIN", &f) != 0)
        return -1;
    /* #52: stamp version + checksum so the read side can detect a
     * torn write of THIS patch (the field layout is unchanged -
     * legacy ver=0 readers just see reserved bytes). */
    g_layout.mb.ver = 1;
    g_layout.mb.cksum = ocire_mb_checksum(&g_layout.mb);
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
