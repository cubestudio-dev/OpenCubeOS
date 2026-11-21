/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocrmecl/efi/ocr_iso9660.c
 * Purpose: minimal ISO9660 reader over the in-RAM copy of the boot
 *          ISO - just enough to locate a root-directory file by its
 *          11-character ISO name (the ";1" version suffix tolerated),
 *          case-insensitively.  OCRMECL only ever looks up OCBL.EFI.
 *
 *          Directory-record layout (ISO 9660:9.1):
 *            +0  u8  record length (0 = end of the directory)
 *            +2  u32 extent LBA (little-endian)
 *            +10 u32 data length (little-endian)
 *            +25 u8  flags (bit 1 = directory)
 *            +32 u8  name length N
 *            +33     the name (N bytes; '\x00' self, '\x01' parent)
 *
 *          xorriso writes every file as ONE contiguous extent, so
 *          (LBA, data length) fully describes the bytes in the RAM
 *          image - exactly what LoadImage-from-buffer needs.
 */
#include "ocr_efi.h"

#define ISO_SECTOR 2048

/* check a sector buffer that IS the PVD (LBA 16) - the whole-image
 * wrapper below adds the offset; the single-sector probe in the boot
 * medium search uses this directly. */
static int pvd_raw(const u8* pvd)
{
    if (pvd[0] != 1)
        return 0;                       /* Primary Volume Descriptor */
    if (pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' ||
        pvd[4] != '0' || pvd[5] != '1')
        return 0;
    return 1;
}

int ocr_iso_pvd(const u8* iso, u64 iso_size)
{
    if (iso_size < 17 * ISO_SECTOR)
        return 0;
    return pvd_raw(iso + 16 * ISO_SECTOR);
}

int ocr_iso_pvd_sector(const u8* sec)
{
    return pvd_raw(sec);
}

static int name_eq(const char* iso_name, u8 len, const char* want)
{
    /* compare case-insensitively, ignoring the ';1' version suffix */
    int i = 0;
    for (; i < (int)len && want[i]; i++) {
        char c = iso_name[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c != want[i] && (want[i] >= 'a' && want[i] <= 'z' ?
                             c != want[i] - 32 : 1))
            return 0;
    }
    if (i < (int)len && iso_name[i] == ';')
        return want[i] == 0;            /* ';1' suffix: consumed */
    return i == (int)len && want[i] == 0;
}

/* Find `want` (uppercase ISO base name, e.g. "OCBL.EFI") in the root
 * directory.  Returns 1 and fills the lba/size out-params on success. */
int ocr_iso_find(const u8* iso, u64 iso_size, const char* want,
                 u64* lba, u64* size)
{
    if (!ocr_iso_pvd(iso, iso_size))
        return 0;
    const u8* pvd = iso + 16 * ISO_SECTOR;
    /* the root directory record sits at PVD offset 156 (34 bytes) */
    const u8* root = pvd + 156;
    u32 rlba = *(const u32*)(root + 2);
    u32 rlen = *(const u32*)(root + 10);
    if (!rlba || !rlen)
        return 0;
    u64 rend = (u64)rlba * ISO_SECTOR + rlen;
    if (rend > iso_size)
        return 0;
    u64 off = (u64)rlba * ISO_SECTOR;
    while (off < rend) {
        const u8* rec = iso + off;
        u8 reclen = rec[0];
        if (reclen == 0) {              /* advance to the next sector */
            off = (off / ISO_SECTOR + 1) * ISO_SECTOR;
            continue;
        }
        u8 nlen = rec[32];
        if (nlen > 1 && !(rec[25] & 2)) {   /* a file, not . / .. / dir */
            if (name_eq((const char*)rec + 33, nlen, want)) {
                *lba = *(const u32*)(rec + 2);
                *size = *(const u32*)(rec + 10);
                return 1;
            }
        }
        off += reclen;
    }
    return 0;
}
