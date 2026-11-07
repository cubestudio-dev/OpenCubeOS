/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_part.c
 * Purpose: MBR + GPT partition table decode (see ocbl_part.h).
 */
#include "ocbl_part.h"
#include "ocbl_disk.h"
#include "ocbl_fs.h"

/* shared scratch: the sector read window doubles as the decode buffer
 * (512 bytes is enough for MBR + protective; GPT headers are 92 bytes
 * valid, entries stream 128 bytes at a time) */
#define PBUF   (ocbl_fs_pbuf())


static u32 rd32(const u8* p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}
static u64 rd64(const u8* p)
{
    return (u64)rd32(p) | ((u64)rd32(p + 4) << 32);
}

const u8 OCBL_GPT_ESP[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B,
};
const u8 OCBL_GPT_LINUXFS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4,
};
const u8 OCBL_GPT_BASIC[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7,
};

static bool read_lba(u8 drive, u32 ssz, u64 lba)
{
    if (ssz < 512)
        return false;
    return ocbl_disk_read(drive, lba, 1, PBUF);
}

static bool guid_eq(const u8* a, const u8* b)
{
    for (int i = 0; i < 16; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

static void add_part(ocbl_part_table_t* t, u64 first, u64 nsec,
                     u32 mbr_type, const u8* guid, const char* name,
                     bool esp, u8 index)
{
    if (t->count >= OCBL_PART_MAX || nsec == 0)
        return;
    ocbl_partition_t* p = &t->parts[t->count++];
    p->first_lba = first;
    p->num_sectors = nsec;
    p->mbr_type = mbr_type;
    p->is_esp = esp;
    p->index = index;
    for (int i = 0; i < 16; i++)
        p->gpt_type[i] = guid ? guid[i] : 0;
    u32 i = 0;
    for (; name && name[i] && i < OCBL_PART_NAME_MAX - 1; i++)
        p->name[i] = name[i];
    p->name[i] = 0;
}

/* GPT entry GUIDs arrive mixed-endian (the first three fields are
 * little-endian in the byte layout we compare against - our GUID
 * constants are already the on-disk byte sequences). */
static void decode_gpt(u8 drive, u32 ssz, ocbl_part_table_t* t)
{
    if (!read_lba(drive, ssz, 1))
        return;
    static const char hdr[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
    for (int i = 0; i < 8; i++)
        if (PBUF[i] != (u8)hdr[i])
            return;
    u64 ent_lba = rd64(PBUF + 72);
    u32 nent = rd32(PBUF + 80);
    u32 ent_size = rd32(PBUF + 84);
    if (nent == 0 || nent > 128 || ent_size < 128 || ent_size > 512)
        return;
    t->kind = OCBL_PT_GPT;
    for (u32 i = 0; i < nent && t->count < OCBL_PART_MAX; i++) {
        u64 e_lba = ent_lba + (u64)i * ent_size / ssz;
        u32 e_off = (u32)((u64)i * ent_size % ssz);
        if (!read_lba(drive, ssz, e_lba))
            break;
        const u8* e = PBUF + e_off;
        const u8* guid = e;
        bool zero = true;
        for (int k = 0; k < 16; k++)
            if (guid[k])
                zero = false;
        if (zero)
            continue;                   /* unused slot */
        u64 first = rd64(e + 32);
        u64 last = rd64(e + 40);
        /* GPT name: 72 bytes UTF-16LE; collapse ASCII, '_' the rest */
        char name[OCBL_PART_NAME_MAX];
        u32 n = 0;
        for (u32 c = 0; c < OCBL_PART_NAME_MAX - 1 && c < 36; c++) {
            u8 lo = e[56 + c * 2];
            u8 hi = e[57 + c * 2];
            if (lo == 0 && hi == 0)
                break;
            name[n++] = hi ? '_' : (char)lo;
        }
        name[n] = 0;
        add_part(t, first, first <= last ? last - first + 1 : 0,
                 0, guid, name, guid_eq(guid, OCBL_GPT_ESP),
                 (u8)(i + 1));
    }
}

bool ocbl_part_scan(u8 drive, u32 ssz, ocbl_part_table_t* out)
{
    out->drive = drive;
    out->kind = OCBL_PT_NONE;
    out->count = 0;
    if (ssz < 512)
        return false;
    if (!read_lba(drive, ssz, 0))
        return false;
    u16 sig = (u16)(PBUF[510] | (PBUF[511] << 8));
    if (sig != 0xAA55)
        return false;

    /* any non-empty MBR slot besides the 0xEE protective one? */
    u32 real = 0;
    u32 shadow = 0;
    for (u32 i = 0; i < 4; i++) {
        const u8* e = PBUF + 0x1BE + i * 16;
        u8 type = e[4];
        if (type == 0)
            continue;
        if (type == 0xEE)
            shadow++;
        else
            real++;
    }
    if (real == 0 && shadow > 0) {
        /* GPT (protective MBR) or the OCIRE shadow shape */
        if (read_lba(drive, ssz, 1)) {
            static const char hdr[8] =
                { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
            bool is_gpt = true;
            for (int i = 0; i < 8; i++)
                if (PBUF[i] != (u8)hdr[i])
                    is_gpt = false;
            if (is_gpt) {
                decode_gpt(drive, ssz, out);
                if (out->kind == OCBL_PT_GPT)
                    return true;
            }
        }
        /* no GPT header: re-read the MBR and decode the 0xEE entry
         * as the shadow volume (read_lba(1) just clobbered PBUF) */
        if (!read_lba(drive, ssz, 0))
            return out->kind = OCBL_PT_SHADOW, true;
        for (u32 i = 0; i < 4; i++) {
            const u8* e = PBUF + 0x1BE + i * 16;
            if (e[4] != 0xEE)
                continue;
            u64 first = rd32(e + 8);
            u64 nsec = rd32(e + 12);
            add_part(out, first, nsec, 0xEE, 0, "ocire-shadow", false,
                     (u8)(i + 1));
        }
        out->kind = OCBL_PT_SHADOW;
        return true;
    }

    out->kind = OCBL_PT_MBR;
    for (u32 i = 0; i < 4; i++) {
        const u8* e = PBUF + 0x1BE + i * 16;
        u8 type = e[4];
        if (type == 0 || type == 0xEE)
            continue;
        u64 first = rd32(e + 8);
        u64 nsec = rd32(e + 12);
        /* first_lba == 0 is impossible for a real partition (the MBR
         * itself lives there); FAT IPL areas can fake a slot */
        if (first == 0)
            continue;
        add_part(out, first, nsec, type, 0, "", type == 0xEF,
                 (u8)(i + 1));
    }
    if (out->count == 0) {
        /* signature present but every slot empty/invalid: a FAT
         * boot sector can carry stray bytes at 0x1BE (mformat IPL
         * area) - this is a whole-disk volume, not a table */
        out->kind = OCBL_PT_NONE;
        return false;
    }
    return true;
}
