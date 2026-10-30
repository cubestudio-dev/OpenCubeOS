/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_fs.c
 * Purpose: read-only FAT12/16/32 + ISO9660 volume layer (see ocbl_fs.h).
 *
 * DESIGN NOTES (declared assumptions):
 *  - The volume sector size equals the drive sector size for the
 *    layouts OCBL scans (FAT on 512-byte disks, ISO9660 on 2048-byte
 *    ATAPI/USB CDs).  All byte offsets still go through byte-granular
 *    windowing (fs_rd), so a FAT BPB bps differing from the drive
 *    bps keeps working.
 *  - No FAT table cache: every FAT-chain lookup reads its own sector
 *    through the shared 2 KiB window (OCBL_FS_SECBUF).  Boot-time
 *    files are small; correctness beats speed.  SECBUF is never used
 *    re-entrantly (single-threaded scan, serial use only).
 *  - FAT chain loops are broken by a hard cluster-visit bound.
 *  - LFN: full multi-fragment assembly (up to 20 fragments / 255
 *    chars), case-insensitive matching against both the LFN and the
 *    8.3 short name.
 */
#include "ocbl_fs.h"
#include "ocbl_disk.h"

static u8* secbuf(void) { return (u8*)OCBL_FS_SECBUF; }

/* ---- little-endian byte readers (no packed-pointer casts) ---- */
static u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8* p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

/* ---- byte-granular read through the shared window ---- */
static bool fs_rd(const ocbl_volume_t* v, u64 byte_off, u32 len, void* out)
{
    u8* dst = (u8*)out;
    u32 dsz = v->sector_size;
    u64 abs_byte = v->base_lba * (u64)dsz + byte_off;
    while (len) {
        u64 dsec = abs_byte / dsz;
        u32 in = (u32)(abs_byte % dsz);
        u32 chunk = dsz - in;
        if (chunk > len)
            chunk = len;
        if (!ocbl_disk_read(v->drive, dsec, 1, secbuf()))
            return false;
        for (u32 i = 0; i < chunk; i++)
            dst[i] = secbuf()[in + i];
        dst += chunk;
        abs_byte += chunk;
        len -= chunk;
    }
    return true;
}

/* Volume-sector-granular read (volume sector = drive sector size). */
static bool fs_rd_vsec(const ocbl_volume_t* v, u64 vsec, u16 count,
                       void* out)
{
    u64 abs_byte = (v->base_lba * (u64)v->sector_size) +
                   (vsec * (u64)v->sector_size);
    if (count > 1)
        return fs_rd(v, vsec * (u64)v->sector_size,
                     (u32)count * v->sector_size, out);
    return ocbl_disk_read(v->drive, abs_byte / v->sector_size, 1, out);
}

/* ==================================================================
 * FAT
 * ================================================================== */
static u64 fat_byte_off(const ocbl_volume_t* v, u32 clus)
{
    if (v->fs == OCBL_VOL_FAT12)
        return (u64)clus + (clus >> 1);
    if (v->fs == OCBL_VOL_FAT16)
        return (u64)clus * 2;
    return (u64)clus * 4;
}

static u32 fat_entry(const ocbl_volume_t* v, u32 clus)
{
    u64 off = fat_byte_off(v, clus);
    u64 sec = v->reserved_sectors + off / v->bytes_per_sector;
    u32 in = (u32)(off % v->bytes_per_sector);
    if (!fs_rd_vsec(v, sec, 1, secbuf()))
        return 0x0FFFFFF8;             /* EOC on read failure */
    if (v->fs == OCBL_VOL_FAT12) {
        u32 b0 = secbuf()[in];
        u32 b1;
        if (in + 1 < (u32)v->bytes_per_sector)
            b1 = secbuf()[in + 1];
        else {
            if (!fs_rd_vsec(v, sec + 1, 1, secbuf()))
                return 0x0FFFFFF8;
            b1 = secbuf()[0];
        }
        u32 val = b0 | (b1 << 8);
        return (clus & 1) ? (val >> 4) : (val & 0xFFF);
    }
    if (v->fs == OCBL_VOL_FAT16)
        return rd16(secbuf() + in);
    return rd32(secbuf() + in) & 0x0FFFFFFF;
}

static bool fat_eoc(const ocbl_volume_t* v, u32 val)
{
    if (v->fs == OCBL_VOL_FAT12)
        return val >= 0xFF8;
    if (v->fs == OCBL_VOL_FAT16)
        return val >= 0xFFF8;
    return val >= 0x0FFFFFF8;
}

static u64 fat_clus_lba(const ocbl_volume_t* v, u32 clus)
{
    return v->data_start + (u64)(clus - 2) * v->sec_per_clus;
}

/* Directory walker state.  clus == 0 selects the FAT12/16 fixed root
 * area; any other value walks the cluster chain. */
typedef struct fat_dir_iter {
    u32 clus;
    u32 idx;            /* entry index inside the current cluster run */
    u32 remaining;      /* entries left in the current cluster run   */
    u64 lba;            /* current run's first volume LBA            */
    u32 sec_per_run;    /* sectors per cluster (root area: 1)        */
} fat_dir_iter_t;

static void fat_dir_begin_run(const ocbl_volume_t* v,
                               fat_dir_iter_t* it)
{
    if (it->clus == 0) {
        it->lba = (u64)v->reserved_sectors +
                  (u64)v->fat_sectors * v->nfats;
        it->remaining = v->root_entries;
        it->sec_per_run = 1;
        return;
    }
    it->lba = fat_clus_lba(v, it->clus);
    it->remaining = (u32)v->sec_per_clus * (v->bytes_per_sector / 32);
    it->sec_per_run = v->sec_per_clus;
}

static bool fat_dir_first(const ocbl_volume_t* v, u32 dir_clus,
                          fat_dir_iter_t* it)
{
    it->idx = 0;
    it->clus = dir_clus;
    fat_dir_begin_run(v, it);
    return it->remaining > 0;
}

static bool fat_dir_next(const ocbl_volume_t* v, fat_dir_iter_t* it,
                         u8 ent[32])
{
    for (;;) {
        while (it->remaining) {
            u32 per_sec = v->bytes_per_sector / 32;
            if ((it->idx % per_sec) == 0) {
                u64 so = it->lba + (u32)(it->idx / per_sec);
                if (!fs_rd_vsec(v, so, 1, secbuf()))
                    return false;
            }
            u32 in = (it->idx % per_sec) * 32;
            it->idx++;
            it->remaining--;
            for (int i = 0; i < 32; i++)
                ent[i] = secbuf()[in + i];
            if (ent[0] == 0x00)
                return false;           /* end-of-directory */
            if (ent[0] == 0xE5)
                continue;               /* deleted */
            return true;
        }
        /* run exhausted: advance the FAT chain (cluster dirs only) */
        if (it->clus < 2)
            return false;
        u32 nxt = fat_entry(v, it->clus);
        if (fat_eoc(v, nxt) || nxt < 2)
            return false;
        it->clus = nxt;
        fat_dir_begin_run(v, it);
    }
}

static void fat_short_name(const u8 ent[32], char* name)
{
    char base[9], ext[4];
    u32 bl = 0, el = 0;
    for (u32 i = 0; i < 8; i++) {
        char c = (char)ent[i];
        if (c == ' ')
            break;
        base[bl++] = c;
    }
    for (u32 i = 8; i < 11; i++) {
        char c = (char)ent[i];
        if (c == ' ')
            break;
        ext[el++] = c;
    }
    u32 o = 0;
    for (u32 i = 0; i < bl; i++)
        name[o++] = base[i];
    if (el) {
        name[o++] = '.';
        for (u32 i = 0; i < el; i++)
            name[o++] = ext[i];
    }
    name[o] = 0;
}

/* ASCII case-insensitive compare */
static bool name_eq(const char* a, const char* b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb)
            return false;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Convert a path segment to an 11-byte 8.3 pattern (space padded). */
static void seg_to_83(const char* seg, u8 pat[11])
{
    for (int i = 0; i < 11; i++)
        pat[i] = ' ';
    u32 o = 0;
    bool dot_seen = false;
    for (u32 i = 0; seg[i]; i++) {
        char c = seg[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == '.') {
            dot_seen = true;
            o = 8;
            continue;
        }
        if (o == 8 && !dot_seen)
            continue;                   /* truncate base name */
        if (o >= 11)
            break;
        pat[o++] = (u8)c;
    }
}

/* LFN fragment capture: fragments arrive in reverse order, the FIRST
 * one (highest seq) has bit 0x40.  We stash each fragment's 13 chars
 * by seq and join 1..total when the short entry lands. */
#define LFN_MAX_PARTS 20
typedef struct fat_lfn_acc {
    u8   parts[LFN_MAX_PARTS][13];
    u8   total;         /* fragments expected (0 = none yet) */
} fat_lfn_acc_t;

static const u8 lfn_byte_off[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};

static void lfn_capture(const u8 ent[32], fat_lfn_acc_t* acc)
{
    u8 seq = ent[0];
    if (seq & 0x40) {
        seq &= 0x1F;
        if (seq == 0 || seq > LFN_MAX_PARTS)
            return;
        acc->total = seq;
    } else if (acc->total) {
        seq &= 0x1F;
        if (seq == 0 || seq > acc->total)
            return;
    } else {
        return;
    }
    u8* dst = acc->parts[seq - 1];
    for (u32 i = 0; i < 13; i++) {
        u8 lo = ent[lfn_byte_off[i]];
        u8 hi = ent[lfn_byte_off[i] + 1];
        /* 0xFF 0xFF padding, 0x00 0x00 terminator */
        if ((lo == 0xFF && hi == 0xFF) || (lo == 0 && hi == 0))
            dst[i] = 0;
        else
            dst[i] = hi ? '_' : lo;
    }
}

static u32 lfn_join(const fat_lfn_acc_t* acc, char* out, u32 max)
{
    if (!acc->total)
        return 0;
    u32 o = 0;
    for (u32 p = 0; p < acc->total && o < max - 1; p++) {
        const u8* frag = acc->parts[p];
        for (u32 i = 0; i < 13 && o < max - 1; i++) {
            char c = (char)frag[i];
            if (c == 0)
                goto done;
            out[o++] = c;
        }
    }
done:
    out[o] = 0;
    return o;
}

/* Resolve `path` from `start_clus` (0 = FAT12/16 root area).  Fills
 * `de` with the located entry; returns false on any miss. */
static bool fat_walk(const ocbl_volume_t* v, u32 start_clus,
                     const char* path, ocbl_fs_dirent_t* de)
{
    char seg[OCBL_FS_NAME_MAX];
    u32 clus = start_clus;

    const char* p = path;
    while (*p == '/') p++;

    while (*p) {
        u32 sl = 0;
        while (p[sl] && p[sl] != '/' && sl < OCBL_FS_NAME_MAX - 1) {
            seg[sl] = p[sl];
            sl++;
        }
        seg[sl] = 0;
        p += sl;
        while (*p == '/') p++;

        u8 ent[32];
        fat_dir_iter_t it;
        if (!fat_dir_first(v, clus, &it))
            return false;
        bool found = false;
        fat_lfn_acc_t acc;
        acc.total = 0;
        while (fat_dir_next(v, &it, ent)) {
            if ((ent[11] & 0x0F) == 0x0F) {
                lfn_capture(ent, &acc);
                continue;
            }
            if (ent[11] & 0x08)
                continue;               /* volume label */
            char lfn[OCBL_FS_NAME_MAX];
            u32 lfn_len = lfn_join(&acc, lfn, OCBL_FS_NAME_MAX);
            acc.total = 0;
            bool hit = false;
            if (lfn_len)
                hit = name_eq(lfn, seg);
            if (!hit) {
                u8 pat[11];
                seg_to_83(seg, pat);
                hit = true;
                for (u32 i = 0; i < 11; i++) {
                    char c = (char)ent[i];
                    if (c >= 'a' && c <= 'z') c += 'A' - 'a';
                    if ((u8)c != pat[i]) { hit = false; break; }
                }
            }
            if (hit) {
                de->attr = ent[11];
                de->size = rd32(ent + 28);
                if (v->fs == OCBL_VOL_FAT32)
                    de->cluster = (rd16(ent + 20) << 16) | rd16(ent + 26);
                else
                    de->cluster = rd16(ent + 26);
                if (lfn_len) {
                    for (u32 i = 0; i < lfn_len; i++)
                        de->name[i] = lfn[i];
                    de->name[lfn_len] = 0;
                } else {
                    fat_short_name(ent, de->name);
                }
                found = true;
                break;
            }
        }
        if (!found)
            return false;
        if (*p) {
            if (!(de->attr & 0x10))
                return false;           /* path descends through file */
            clus = de->cluster;
        } else {
            return true;
        }
    }
    return true;
}

/* ---- FAT mount ---- */
static bool fat_mount(u8 drive, u64 base_lba, u32 dsz, ocbl_volume_t* v)
{
    ocbl_volume_t probe;
    probe.drive = drive;
    probe.base_lba = base_lba;
    probe.sector_size = dsz;
    if (!fs_rd_vsec(&probe, 0, 1, secbuf()))
        return false;
    u16 bps = rd16(secbuf() + 11);
    u8 spc = secbuf()[13];
    u16 resvd = rd16(secbuf() + 14);
    u8 nfats = secbuf()[16];
    u16 rootent = rd16(secbuf() + 17);
    u16 tot16 = rd16(secbuf() + 19);
    u16 fatsz16 = rd16(secbuf() + 22);
    u32 tot32 = rd32(secbuf() + 32);
    u32 fatsz32 = rd32(secbuf() + 36);
    u32 rootclus = rd32(secbuf() + 44);

    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096)
        return false;
    if (spc == 0 || (spc & (spc - 1)) != 0)
        return false;
    if (nfats == 0 || nfats > 4)
        return false;
    u32 totsec = tot16 ? tot16 : tot32;
    if (totsec == 0)
        return false;
    u32 fatsz = fatsz16 ? fatsz16 : fatsz32;
    if (fatsz == 0)
        return false;
    u32 rootdirsec = ((u32)rootent * 32 + bps - 1) / bps;
    if (totsec < (u64)resvd + (u64)fatsz * nfats + rootdirsec)
        return false;
    u64 datasec = totsec - (u64)resvd - (u64)fatsz * nfats - rootdirsec;
    u32 clusters = (u32)(datasec / spc);
    if (clusters < 4085)
        v->fs = OCBL_VOL_FAT12;
    else if (clusters < 65525)
        v->fs = OCBL_VOL_FAT16;
    else
        v->fs = OCBL_VOL_FAT32;
    if (v->fs != OCBL_VOL_FAT32 && rootent == 0)
        return false;
    if (v->fs == OCBL_VOL_FAT32 && rootclus < 2)
        return false;

    v->drive = drive;
    v->base_lba = base_lba;
    v->sector_size = dsz;
    v->bytes_per_sector = bps;
    v->sec_per_clus = spc;
    v->reserved_sectors = resvd;
    v->nfats = nfats;
    v->fat_sectors = fatsz;
    v->root_cluster = v->fs == OCBL_VOL_FAT32 ? rootclus : 0;
    v->root_entries = rootent;
    v->data_start = (u64)resvd + (u64)fatsz * nfats + rootdirsec;
    v->total_sectors = totsec;
    return true;
}

/* ---- ISO9660 ---- */
static bool iso_mount(u8 drive, u64 base_lba, u32 dsz, ocbl_volume_t* v)
{
    if (dsz != 2048)
        return false;
    ocbl_volume_t probe;
    probe.drive = drive;
    probe.base_lba = base_lba;
    probe.sector_size = dsz;
    if (!fs_rd_vsec(&probe, 16, 1, secbuf()))
        return false;
    if (secbuf()[0] != 1)
        return false;
    static const char sig[5] = { 'C', 'D', '0', '0', '1' };
    for (int i = 0; i < 5; i++)
        if (secbuf()[1 + i] != (u8)sig[i])
            return false;
    const u8* root = secbuf() + 156;
    if (root[0] == 0 || root[0] > 128)
        return false;
    v->fs = OCBL_VOL_ISO9660;
    v->drive = drive;
    v->base_lba = base_lba;
    v->sector_size = dsz;
    v->iso_root_lba = rd32(root + 2);
    v->iso_root_size = rd32(root + 10);
    return true;
}

static bool iso_walk(const ocbl_volume_t* v, const char* path,
                     u64* out_lba, u32* out_size, bool* out_dir)
{
    static u8 isobuf[8192];             /* directory extent window */
    u64 dir_lba = v->iso_root_lba;
    u32 dir_size = v->iso_root_size;
    if (dir_size > sizeof(isobuf))
        dir_size = sizeof(isobuf);

    const char* p = path;
    while (*p == '/') p++;
    while (*p) {
        char seg[OCBL_FS_NAME_MAX];
        u32 sl = 0;
        while (p[sl] && p[sl] != '/' && sl < OCBL_FS_NAME_MAX - 1) {
            seg[sl] = p[sl];
            sl++;
        }
        seg[sl] = 0;
        p += sl;
        while (*p == '/') p++;

        if (!fs_rd_vsec(v, dir_lba, (u16)((dir_size + 2047) / 2048),
                        isobuf))
            return false;
        bool found = false;
        bool isdir = false;
        u64 hit_lba = 0;
        u32 hit_size = 0;
        u32 off = 0;
        while (off + 34 <= dir_size) {
            u8 rec_len = isobuf[off];
            if (rec_len == 0) {
                off = (off / 2048 + 1) * 2048;   /* no sector span */
                continue;
            }
            if (off + rec_len > dir_size)
                break;
            u32 nl = isobuf[off + 32];
            const u8* nm = isobuf + off + 33;
            u8 flags = isobuf[off + 25];
            u32 cmp_len = nl;
            if (cmp_len >= 2 && nm[cmp_len - 2] == ';')
                cmp_len -= 2;
            bool hit = cmp_len == sl;
            for (u32 i = 0; hit && i < sl; i++) {
                char a = (char)nm[i], b = seg[i];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b)
                    hit = false;
            }
            if (hit) {
                hit_lba = rd32(isobuf + off + 2);
                hit_size = rd32(isobuf + off + 10);
                isdir = (flags & 2) != 0;
                found = true;
                break;
            }
            off += rec_len;
        }
        if (!found)
            return false;
        if (!*p) {
            if (out_lba)
                *out_lba = hit_lba;
            if (out_size)
                *out_size = hit_size;
            if (out_dir)
                *out_dir = isdir;
            return true;
        }
        if (!isdir)
            return false;               /* descend through a file */
        dir_lba = hit_lba;
        dir_size = hit_size;
        if (dir_size > sizeof(isobuf))
            dir_size = sizeof(isobuf);
    }
    return false;
}

/* ---- public API ---- */
bool ocbl_fs_mount(u8 drive, u64 base_lba, u32 sector_size,
                   ocbl_volume_t* v)
{
    v->fs = OCBL_VOL_NONE;
    if (sector_size == 0)
        return false;
    if (fat_mount(drive, base_lba, sector_size, v))
        return true;
    return iso_mount(drive, base_lba, sector_size, v);
}

bool ocbl_fs_stat(const ocbl_volume_t* v, const char* path, u32* size)
{
    if (v->fs == OCBL_VOL_ISO9660) {
        u64 lba = 0;
        u32 sz = 0;
        bool isdir = false;
        if (!iso_walk(v, path, &lba, &sz, &isdir) || isdir)
            return false;
        if (size)
            *size = sz;
        return true;
    }
    if (v->fs == OCBL_VOL_NONE)
        return false;
    ocbl_fs_dirent_t de;
    u32 start = (v->fs == OCBL_VOL_FAT32) ? v->root_cluster : 0;
    if (!fat_walk(v, start, path, &de))
        return false;
    if (de.attr & 0x10)
        return false;
    if (size)
        *size = de.size;
    return true;
}

u32 ocbl_fs_read_file(const ocbl_volume_t* v, const char* path,
                      void* buf, u32 max)
{
    if (v->fs == OCBL_VOL_ISO9660) {
        u64 lba = 0;
        u32 sz = 0;
        bool isdir = false;
        if (!iso_walk(v, path, &lba, &sz, &isdir) || isdir)
            return 0;
        u32 want = sz < max ? sz : max;
        u16 nsec = (u16)((want + 2047) / 2048);
        u8* dst = (u8*)buf;
        for (u16 s = 0; s < nsec; s++) {
            if (!fs_rd_vsec(v, lba + s, 1, secbuf()))
                return 0;
            u32 base = (u32)s * 2048;
            u32 chunk = want - base;
            if (chunk > 2048)
                chunk = 2048;
            for (u32 i = 0; i < chunk; i++)
                dst[base + i] = secbuf()[i];
        }
        return want;
    }
    if (v->fs == OCBL_VOL_NONE)
        return 0;
    ocbl_fs_dirent_t de;
    u32 start = (v->fs == OCBL_VOL_FAT32) ? v->root_cluster : 0;
    if (!fat_walk(v, start, path, &de))
        return 0;
    if (de.attr & 0x10)
        return 0;
    u32 clus = de.cluster;
    u32 left = de.size < max ? de.size : max;
    u32 done = 0;
    u32 guard = 0;
    while (left && clus >= 2 && !fat_eoc(v, clus)) {
        if (++guard > 200000)
            break;                      /* chain loop bound */
        u64 lba = fat_clus_lba(v, clus);
        for (u32 s = 0; s < v->sec_per_clus && left; s++) {
            u32 chunk = left < v->bytes_per_sector ? left
                                                   : v->bytes_per_sector;
            if (!fs_rd_vsec(v, lba + s, 1, secbuf()))
                return 0;
            for (u32 i = 0; i < chunk; i++)
                ((u8*)buf)[done + i] = secbuf()[i];
            done += chunk;
            left -= chunk;
        }
        clus = fat_entry(v, clus);
    }
    return done;
}

u32 ocbl_fs_pread(const ocbl_volume_t* v, const char* path, u32 off,
                  void* buf, u32 len)
{
    if (v->fs == OCBL_VOL_ISO9660) {
        u64 lba = 0;
        u32 sz = 0;
        bool isdir = false;
        if (!iso_walk(v, path, &lba, &sz, &isdir) || isdir)
            return 0;
        if (off >= sz)
            return 0;
        u32 want = sz - off;
        if (want > len)
            want = len;
        u32 done = 0;
        u32 sec = off / 2048;
        u32 in = off % 2048;
        u8* dst = (u8*)buf;
        while (done < want) {
            if (!fs_rd_vsec(v, lba + sec, 1, secbuf()))
                return done;
            u32 chunk = 2048 - in;
            if (chunk > want - done)
                chunk = want - done;
            for (u32 i = 0; i < chunk; i++)
                dst[done + i] = secbuf()[in + i];
            done += chunk;
            sec++;
            in = 0;
        }
        return done;
    }
    if (v->fs == OCBL_VOL_NONE)
        return 0;
    ocbl_fs_dirent_t de;
    u32 start = (v->fs == OCBL_VOL_FAT32) ? v->root_cluster : 0;
    if (!fat_walk(v, start, path, &de))
        return 0;
    if (de.attr & 0x10)
        return 0;
    if (off >= de.size)
        return 0;
    u32 want = de.size - off;
    if (want > len)
        want = len;
    u32 clus_bytes = (u32)v->sec_per_clus * v->bytes_per_sector;
    /* skip to the cluster holding `off` */
    u32 skip = off / clus_bytes;
    u32 in_clus = off % clus_bytes;
    u32 clus = de.cluster;
    while (skip && clus >= 2 && !fat_eoc(v, clus)) {
        clus = fat_entry(v, clus);
        skip--;
    }
    u32 done = 0;
    u8* dst = (u8*)buf;
    u32 guard = 0;
    while (done < want && clus >= 2 && !fat_eoc(v, clus)) {
        if (++guard > 200000)
            break;
        u64 lba = fat_clus_lba(v, clus) + in_clus / v->bytes_per_sector;
        u32 in = in_clus % v->bytes_per_sector;
        u32 take = clus_bytes - in_clus;
        if (take > want - done)
            take = want - done;
        u32 got = 0;
        while (got < take) {
            u32 chunk = v->bytes_per_sector - in;
            if (chunk > take - got)
                chunk = take - got;
            if (!fs_rd_vsec(v, lba, 1, secbuf()))
                return done;
            for (u32 i = 0; i < chunk; i++)
                dst[done + got + i] = secbuf()[in + i];
            got += chunk;
            in = 0;
            lba++;
        }
        done += take;
        in_clus = 0;
        clus = fat_entry(v, clus);
    }
    return done;
}

u32 ocbl_fs_list_dir(const ocbl_volume_t* v, const char* path,
                     ocbl_fs_dirent_t* out, u32 max)
{
    if (v->fs != OCBL_VOL_FAT12 && v->fs != OCBL_VOL_FAT16 &&
        v->fs != OCBL_VOL_FAT32)
        return 0;
    u32 start = (v->fs == OCBL_VOL_FAT32) ? v->root_cluster : 0;
    if (path && path[0]) {
        ocbl_fs_dirent_t de;
        if (!fat_walk(v, start, path, &de) || !(de.attr & 0x10))
            return 0;
        start = de.cluster;
    }
    u8 ent[32];
    fat_dir_iter_t it;
    if (!fat_dir_first(v, start, &it))
        return 0;
    u32 n = 0;
    fat_lfn_acc_t acc;
    acc.total = 0;
    while (n < max && fat_dir_next(v, &it, ent)) {
        if ((ent[11] & 0x0F) == 0x0F) {
            lfn_capture(ent, &acc);
            continue;
        }
        if (ent[11] & 0x08)
            continue;
        ocbl_fs_dirent_t* d = &out[n];
        d->attr = ent[11];
        d->size = rd32(ent + 28);
        if (v->fs == OCBL_VOL_FAT32)
            d->cluster = (rd16(ent + 20) << 16) | rd16(ent + 26);
        else
            d->cluster = rd16(ent + 26);
        char lfn[OCBL_FS_NAME_MAX];
        u32 lfn_len = lfn_join(&acc, lfn, OCBL_FS_NAME_MAX);
        acc.total = 0;
        if (lfn_len) {
            for (u32 i = 0; i < lfn_len; i++)
                d->name[i] = lfn[i];
            d->name[lfn_len] = 0;
        } else {
            fat_short_name(ent, d->name);
        }
        n++;
    }
    return n;
}

void ocbl_fs_describe(const ocbl_volume_t* v, char* out, u32 max)
{
    static const char* names[] = { "?", "FAT12", "FAT16", "FAT32",
                                   "ISO9660" };
    const char* fs = names[v->fs <= OCBL_VOL_ISO9660 ? v->fs : 0];
    u32 o = 0;
    while (*fs && o < max - 1)
        out[o++] = *fs++;
    if (o < max - 1)
        out[o++] = ' ';
    if (o < max - 1) {
        out[o++] = v->drive >= 0xE0 ? 'c' : 'h';
        if (o < max - 1)
            out[o++] = 'd';              /* both "hd" and "cd" */
    }
    if (o < max - 1)
        out[o++] = (char)('0' + (v->drive & 0x0F));
    if (o < max - 1)
        out[o++] = '@';
    char buf[17];
    u64 x = v->base_lba;
    u32 i = 0;
    do {
        u8 d = (u8)(x & 0xF);
        buf[i++] = (char)('0' + d + (d < 10 ? 0 : 'A' - '0' - 10));
        x >>= 4;
    } while (x && i < 16);
    while (i && o < max - 1)
        out[o++] = buf[--i];
    out[o] = 0;
}
