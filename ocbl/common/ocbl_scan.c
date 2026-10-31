/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_scan.c
 * Purpose: the boot-entry scanner (see ocbl_scan.h for the matrix).
 */
#include "ocbl_scan.h"
#include "ocbl_part.h"
#include "ocbl_disk.h"
#include "ocbl_mmap.h"
#include "ocbl_serial.h"

static ocbl_scan_t ocbl_scan_table;

ocbl_scan_t* ocbl_scan_result(void)
{
    return &ocbl_scan_table;
}

#define MB2_MAGIC 0xE85250D6u

/* OCOS A/B slot protocol: when the first OCOS volume of a disk
 * carries the flag files, partitions 2/3 of that disk are consumed
 * as slot kernels - they must not register their own entries. */
static u8 ab_slot_drive;   /* drive whose slots were consumed (0 = none) */

/* multiboot2 header presence: the header must live in the first
 * 32764 bytes of the file (spec); we scan the first 8 KiB, which
 * covers every layout our kernels generate. */
static bool file_has_mb2_header(const ocbl_volume_t* v, const char* path)
{
    static u8 probe[8192];
    u32 n = ocbl_fs_read_file(v, path, probe, sizeof(probe));
    if (n < 64)
        return false;
    for (u32 off = 0; off + 4 <= n; off += 4)
        if ((u32)probe[off] == 0xD6 &&
            (u32)probe[off + 1] == 0x50 &&
            (u32)probe[off + 2] == 0x52 &&
            (u32)probe[off + 3] == 0xE8)
            return true;    /* 0xE85250D6 little-endian */
    return false;
}

/* ---- label helpers ---- */
typedef struct label_ctx {
    char* buf;
    u32   cap;
    u32   len;
} label_ctx_t;

static void lab_init(label_ctx_t* l, char* buf, u32 cap)
{
    l->buf = buf;
    l->cap = cap;
    l->len = 0;
    buf[0] = 0;
}

static void lab_add(label_ctx_t* l, const char* s)
{
    while (*s && l->len < l->cap - 1)
        l->buf[l->len++] = *s++;
    l->buf[l->len] = 0;
}

static void lab_add_u(label_ctx_t* l, u32 val)
{
    char tmp[12];
    u32 i = 0;
    do {
        tmp[i++] = (char)('0' + val % 10);
        val /= 10;
    } while (val && i < sizeof(tmp) - 1);
    while (i && l->len < l->cap - 1)
        l->buf[l->len++] = tmp[--i];
    l->buf[l->len] = 0;
}

static void lab_drive(label_ctx_t* l, u8 drive)
{
    if (drive >= 0xE0)
        lab_add(l, "cd");
    else
        lab_add(l, "hd");
    lab_add_u(l, drive & 0x0F);
}

static void lab_part(label_ctx_t* l, const ocbl_partition_t* pt,
                     u8 table_kind)
{
    lab_add(l, table_kind == OCBL_PT_GPT ? " gpt" : " p");
    lab_add_u(l, pt ? pt->index : 0);
}

/* ---- entry registration ---- */
static ocbl_entry_t* add_entry(u8 type)
{
    ocbl_scan_t* t = &ocbl_scan_table;
    if (t->count >= OCBL_MAX_ENTRIES)
        return 0;
    ocbl_entry_t* e = &t->entries[t->count++];
    for (u32 i = 0; i < sizeof(*e); i++)
        ((u8*)e)[i] = 0;
    e->type = type;
    return e;
}

static void fill_vol(ocbl_entry_t* e, const ocbl_volume_t* v)
{
    e->drive = v->drive;
    e->ssz = v->sector_size;
    e->base_lba = v->base_lba;
    e->fs = v->fs;
}

/* Copy a volume-relative path (bounded). */
static void set_path(ocbl_entry_t* e, const char* path)
{
    u32 i = 0;
    for (; path[i] && i < sizeof(e->path) - 1; i++)
        e->path[i] = path[i];
    e->path[i] = 0;
}

static void set_efi(ocbl_entry_t* e, const char* path)
{
    u32 i = 0;
    for (; path[i] && i < sizeof(e->efi_path) - 1; i++)
        e->efi_path[i] = path[i];
    e->efi_path[i] = 0;
}

/* ---- the six-class judge for one mounted volume ----
 * Short-circuits in priority order: OCOS > OCIRE > OCSE > WINDOWS >
 * LINUX > GENERIC.  Returns the type found (0 = nothing). */
static u32 judge_volume(const ocbl_volume_t* v,
                        const ocbl_partition_t* pt, u8 table_kind,
                        ocbl_entry_t** out)
{
    u32 size = 0;

    /* OCSE FIRST (B5-7 live-caught): a marker-carrying install medium
     * is OCSE even when it also ships a kernel - the class is the
     * marker's, the ordering below settles the precedence. */
    /* OCSE: the install media marker (WP-06 contract; the 8.3-safe
     * name survives every plain-ISO9660 mangling xorriso can apply) */
    if (v->fs == OCBL_VOL_ISO9660 &&
        ocbl_fs_stat(v, "/ocse.mrk", &size)) {
        ocbl_entry_t* e = add_entry(OCBL_ENT_OCSE);
        if (e) {
            fill_vol(e, v);
            e->file_size = size;
            label_ctx_t l;
            lab_init(&l, e->label, OCBL_LABEL_MAX);
            lab_add(&l, "OCSE install media (");
            lab_drive(&l, v->drive);
            lab_add(&l, ")");
            *out = e;
        }
        return OCBL_ENT_OCSE;
    }


    /* OCOS: the multiboot2 system kernel.  ISO volumes are NOT
     * excluded anymore (B5-7): the release opencube.iso IS the
     * bootable OCOS itself - a marker-less ISO carrying
     * /boot/opencube.elf registers as "Open Cube OS (cd0)". */
    if (ocbl_fs_stat(v, "/boot/opencube.elf", &size) &&
        file_has_mb2_header(v, "/boot/opencube.elf")) {
        /* slot-consumed volumes belong to an already-registered
         * A/B entry on the same disk */
        if (ab_slot_drive == v->drive && pt &&
            (pt->index == 2 || pt->index == 3))
            return OCBL_ENT_OCOS;
        ocbl_entry_t* e = add_entry(OCBL_ENT_OCOS);
        if (!e)
            return OCBL_ENT_OCOS;
        fill_vol(e, v);
        e->file_size = size;
        set_path(e, "/boot/opencube.elf");

        /* A/B slot protocol (native GRUB-semantics replacement):
         * flag files on the SAME volume select the slot kernel on
         * partitions 2/3 of the same disk. */
        label_ctx_t l;
        lab_init(&l, e->label, OCBL_LABEL_MAX);
        lab_add(&l, "Open Cube OS");
        if (ocbl_fs_stat(v, "/boot/next_B", 0) ||
            ocbl_fs_stat(v, "/boot/ok_B", 0) ||
            ocbl_fs_stat(v, "/boot/bootfail_B", 0)) {
            bool next_b = ocbl_fs_stat(v, "/boot/next_B", 0);
            bool bootfail = ocbl_fs_stat(v, "/boot/bootfail_B", 0);
            bool ok_b = ocbl_fs_stat(v, "/boot/ok_B", 0);
            /* slot B volume = partition 3, slot A = partition 2 */
            u32 want = (next_b && !bootfail) || (!next_b && ok_b)
                           ? 3 : 2;
            e->slot = (u8)(want == 3 ? 2 : 1);
            ab_slot_drive = v->drive;
            /* resolve the slot volume on the same disk */
            ocbl_part_table_t pt2;
            if (ocbl_part_scan(v->drive, v->sector_size, &pt2)) {
                for (u32 i = 0; i < pt2.count; i++) {
                    ocbl_volume_t sv;
                    if (pt2.parts[i].index != want)
                        continue;
                    if (!ocbl_fs_mount(v->drive,
                                       pt2.parts[i].first_lba,
                                       v->sector_size, &sv))
                        continue;
                    u32 ksize = 0;
                    if (sv.fs != OCBL_VOL_ISO9660 &&
                        ocbl_fs_stat(&sv, "/boot/opencube.elf",
                                     &ksize)) {
                        e->drive = sv.drive;
                        e->base_lba = sv.base_lba;
                        e->fs = sv.fs;
                        e->ssz = sv.sector_size;
                        e->file_size = ksize;
                    }
                }
            }
            lab_add(&l, " (slot ");
            lab_add(&l, want == 3 ? "B" : "A");
            lab_add(&l, ", ");
            lab_drive(&l, v->drive);
            lab_add(&l, ")");
            if (next_b && bootfail)
                lab_add(&l, " rollback");
        } else {
            e->slot = 0;
            lab_add(&l, " (");
            lab_drive(&l, v->drive);
            if (pt)
                if (pt) lab_part(&l, pt, table_kind);
            lab_add(&l, ")");
        }
        *out = e;
        return OCBL_ENT_OCOS;
    }

    /* OCIRE: the recovery kernel (shadow volume, plain volume, or the
     * standalone recovery ISO - B5-7 opens the ISO gate here too) */
    if (ocbl_fs_stat(v, "/boot/ocire.elf", &size)) {
        ocbl_entry_t* e = add_entry(OCBL_ENT_OCIRE);
        if (e) {
            fill_vol(e, v);
            e->file_size = size;
            set_path(e, "/boot/ocire.elf");
            label_ctx_t l;
            lab_init(&l, e->label, OCBL_LABEL_MAX);
            lab_add(&l, "OCIRE recovery (");
            lab_drive(&l, v->drive);
            if (pt) lab_part(&l, pt, table_kind);
            lab_add(&l, ")");
            *out = e;
        }
        return OCBL_ENT_OCIRE;
    }

    /* WINDOWS: the MS boot manager on an ESP */
    if (v->fs != OCBL_VOL_ISO9660 &&
        ocbl_fs_stat(v, "/EFI/Microsoft/Boot/bootmgfw.efi", &size)) {
        ocbl_entry_t* e = add_entry(OCBL_ENT_WINDOWS);
        if (e) {
            fill_vol(e, v);
            e->file_size = size;
            set_efi(e, "/EFI/Microsoft/Boot/bootmgfw.efi");
            label_ctx_t l;
            lab_init(&l, e->label, OCBL_LABEL_MAX);
            lab_add(&l, "Windows (");
            lab_drive(&l, v->drive);
            if (pt) lab_part(&l, pt, table_kind);
            lab_add(&l, ")");
            *out = e;
        }
        return OCBL_ENT_WINDOWS;
    }

    /* LINUX: foreign EFI loaders on an ESP (ext roots are out of
     * scope in 3.0.0 - declared limit) */
    if (v->fs != OCBL_VOL_ISO9660 && pt && pt->is_esp) {
        ocbl_fs_dirent_t dirs[16];
        u32 n = ocbl_fs_list_dir(v, "/EFI", dirs, 16);
        for (u32 i = 0; i < n; i++) {
            if (!(dirs[i].attr & 0x10))
                continue;
            char sub[96];
            u32 o = 0;
            const char* pre = "/EFI/";
            for (u32 k = 0; pre[k]; k++)
                sub[o++] = pre[k];
            for (u32 k = 0; dirs[i].name[k] && o < 92; k++)
                sub[o++] = dirs[i].name[k];
            sub[o] = 0;
            static const char* cand[3] = {
                "/grubx64.efi", "/shimx64.efi", "/systemd-bootx64.efi"
            };
            for (u32 c = 0; c < 3; c++) {
                u32 k = o;
                const char* s = cand[c];
                for (u32 t = 0; s[t] && k < 95; t++)
                    sub[k++] = s[t];
                sub[k] = 0;
                u32 lsize = 0;
                if (!ocbl_fs_stat(v, sub, &lsize))
                    continue;
                /* BOOT is the generic vendor dir - skip it here, it
                 * falls through to the GENERIC rule below */
                bool is_boot = false;
                const char* dn = dirs[i].name;
                if (dn[0] == 'B' && dn[1] == 'O' && dn[2] == 'O' &&
                    dn[3] == 'T' && dn[4] == 0)
                    is_boot = true;
                if (is_boot)
                    break;
                ocbl_entry_t* e = add_entry(OCBL_ENT_LINUX);
                if (e) {
                    fill_vol(e, v);
                    e->file_size = lsize;
                    set_efi(e, sub);
                    label_ctx_t l2;
                    lab_init(&l2, e->label, OCBL_LABEL_MAX);
                    lab_add(&l2, "Linux: ");
                    lab_add(&l2, dirs[i].name);
                    lab_add(&l2, " (");
                    lab_drive(&l2, v->drive);
                    if (pt) lab_part(&l2, pt, table_kind);
                    lab_add(&l2, ")");
                    *out = e;
                }
                return OCBL_ENT_LINUX;
            }
        }
    }

    /* GENERIC: removable media / EFI shells / anything else with a
     * standard removable-media loader */
    if (v->fs != OCBL_VOL_ISO9660 &&
        ocbl_fs_stat(v, "/EFI/BOOT/BOOTX64.EFI", &size)) {
        ocbl_entry_t* e = add_entry(OCBL_ENT_GENERIC);
        if (e) {
            fill_vol(e, v);
            e->file_size = size;
            set_efi(e, "/EFI/BOOT/BOOTX64.EFI");
            label_ctx_t l;
            lab_init(&l, e->label, OCBL_LABEL_MAX);
            lab_add(&l, "EFI loader (");
            lab_drive(&l, v->drive);
            if (pt) lab_part(&l, pt, table_kind);
            lab_add(&l, ")");
            *out = e;
        }
        return OCBL_ENT_GENERIC;
    }

    return 0;
}

/* Scan one drive: partition sweep + whole-disk fallback. */
static void scan_drive(u8 drive, u32 ssz)
{
    ocbl_part_table_t pt;
    bool have_table = ocbl_part_scan(drive, ssz, &pt);

    if (have_table && pt.kind != OCBL_PT_NONE) {
        u32 before = ocbl_scan_table.count;
        for (u32 i = 0; i < pt.count; i++) {
            ocbl_volume_t v;
            if (!ocbl_fs_mount(drive, pt.parts[i].first_lba,
                               ssz, &v))
                continue;
            ocbl_entry_t* e = 0;
            judge_volume(&v, &pt.parts[i], pt.kind, &e);
        }
        if (ocbl_scan_table.count != before)
            return;                     /* partitions decided it */
        /* fall through: a "partition table" that yielded nothing
         * (or a corrupted slot layout) may still sit on a whole-disk
         * filesystem */
    }

    /* no partition table: whole-disk filesystem (floppy/superfloppy
     * shape - the OCIRE 16 MiB shadow also decodes this way when the
     * 0xEE entry itself is the volume) */
    ocbl_volume_t v;
    if (ocbl_fs_mount(drive, 0, ssz, &v)) {
        ocbl_entry_t* e = 0;
        judge_volume(&v, 0, OCBL_PT_NONE, &e);
    }
}

u32 ocbl_scan_all(void)
{
    ocbl_scan_table.count = 0;
    ab_slot_drive = 0;

    /* the flavor enumerates the disks (BIOS: the int 13h ranges with
     * geom-gated presence; UEFI: the BlockIo handle table), the rest
     * of the sweep is pure common logic */
    u8 drives[32];
    u32 n = ocbl_disk_enumerate(drives, 32);
    for (u32 i = 0; i < n; i++) {
        u8 d = drives[i];
        u64 total = 0;
        u16 bps = 0;
        if (!ocbl_disk_geom(d, &total, &bps))
            continue;
        if (bps == 0)
            bps = 512;
        if (ocbl_disk_kind(d) && bps != 2048)
            continue;               /* cdrom-like must be 2048 B */
        scan_drive(d, bps);
    }

    return ocbl_scan_table.count;
}

void ocbl_scan_dump(void)
{
    static const char* tname[] = {
        "none", "OCOS", "OCIRE", "OCSE", "WINDOWS", "LINUX",
        "GENERIC", "SHUTDOWN"
    };
    ocbl_serial_puts("[scan] entries: ");
    ocbl_serial_dec(ocbl_scan_table.count);
    ocbl_serial_puts("\r\n");
    for (u32 i = 0; i < ocbl_scan_table.count; i++) {
        ocbl_entry_t* e = &ocbl_scan_table.entries[i];
        ocbl_serial_puts("  [");
        ocbl_serial_dec(i);
        ocbl_serial_puts("] ");
        ocbl_serial_puts(tname[e->type <= OCBL_ENT_SHUTDOWN
                                   ? e->type : 0]);
        ocbl_serial_puts(": ");
        ocbl_serial_puts(e->label);
        if (e->path[0]) {
            ocbl_serial_puts("  path=");
            ocbl_serial_puts(e->path);
            ocbl_serial_puts(" size=");
            ocbl_serial_dec(e->file_size);
        }
        if (e->efi_path[0]) {
            ocbl_serial_puts("  efi=");
            ocbl_serial_puts(e->efi_path);
        }
        if (e->slot)
            ocbl_serial_puts(e->slot == 2 ? "  slot=B" : "  slot=A");
        ocbl_serial_puts("\r\n");
    }
}
