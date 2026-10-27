/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_fat.c
 * Purpose: FAT12 + FAT32 volume access (see ocire_fat.h).
 *
 * On-disk structures follow Microsoft's FAT32 specification (fatgen
 * 1.03): BPB fields at fixed offsets, 32-byte directory entries,
 * 0x0FFFFFF8..F end-of-chain markers, 0xE5 deleted-entry markers.
 * FAT12 keeps the classic fixed root-directory area and 12-bit FAT
 * entries; LFN (attr 0x0F) entries are skipped on sight.
 */

#include "ocire_fat.h"
#include "ocire_ata.h"
#include "ocire_util.h"

#define DIRENT_SIZE 32
#define ATTR_LFN  0x0F
#define ATTR_DIR  0x10
#define DELETED   0xE5

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) {
    return (u32)(p[0] | (p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24));
}
static void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* ---- sector cache (one sector, refilled per access) ---- */

static u8 g_sec[512];
static u64 g_sec_lba = ~0ull;
static int g_sec_idx = -1;

static int sec_read(int ata_idx, u64 lba) {
    if (g_sec_idx == ata_idx && g_sec_lba == lba) return 0;
    if (ocire_ata_read(ata_idx, lba, 1, g_sec) != 0) return -1;
    g_sec_idx = ata_idx;
    g_sec_lba = lba;
    return 0;
}

int ocire_fat_read_sect(ocire_fat_vol_t *vol, u64 abs_lba, void *buf) {
    if (ocire_ata_read(vol->ata_idx, abs_lba, 1, buf) != 0) return -1;
    return 0;
}

int ocire_fat_write_sect(ocire_fat_vol_t *vol, u64 abs_lba, const void *buf) {
    if (ocire_ata_write(vol->ata_idx, abs_lba, 1, buf) != 0) return -1;
    if (g_sec_idx == vol->ata_idx && g_sec_lba == abs_lba) {
        oc_memcpy(g_sec, buf, 512);            /* keep the cache honest */
    }
    return 0;
}

/* ---- mount ---- */

int ocire_fat_mount(ocire_fat_vol_t *vol, int ata_idx, u64 part_lba) {
    oc_memset(vol, 0, sizeof(*vol));
    u8 bpb[512];
    if (ocire_ata_read(ata_idx, part_lba, 1, bpb) != 0) return -1;
    if (rd16(bpb + 510) != 0xAA55) return -2;   /* boot signature */

    u32 bps = rd16(bpb + 11);
    u32 spc = bpb[13];
    u32 reserved = rd16(bpb + 14);
    u32 nfats = bpb[16];
    u32 rootent = rd16(bpb + 17);
    u32 totsec16 = rd16(bpb + 19);
    u32 fatsz16 = rd16(bpb + 22);
    u32 totsec32 = rd32(bpb + 32);
    u32 fatsz32 = rd32(bpb + 36);
    u32 rootclus = rd32(bpb + 44);

    if (bps != 512 || spc == 0 || nfats == 0 || reserved == 0) return -3;
    u64 totsec = totsec16 ? totsec16 : totsec32;
    if (totsec < 64) return -4;

    u32 fat_secs = fatsz16 ? fatsz16 : fatsz32;
    if (fat_secs == 0) return -5;

    u64 root_secs = (rootent * DIRENT_SIZE + 511) / 512;
    u64 first_data = reserved + (u64)nfats * fat_secs + root_secs;
    if (totsec <= first_data) return -6;
    u64 data_secs = totsec - first_data;
    u32 clusters = (u32)(data_secs / spc);

    vol->ata_idx = ata_idx;
    vol->part_lba = part_lba;
    vol->bytes_per_sec = bps;
    vol->sec_per_clus = spc;
    vol->cluster_secs = spc;
    vol->reserved_secs = reserved;
    vol->nfats = nfats;
    vol->fat_secs = fat_secs;
    vol->fat_start = reserved;
    vol->data_start = first_data;
    vol->total_clusters = clusters;
    vol->root_dir_sectors = root_secs;

    if (rootent != 0 && fatsz16 != 0 && clusters < 4085) {
        vol->fat_type = 12;
        vol->root_cluster = 0;
        return 0;
    }
    if (rootent == 0 && fatsz16 == 0 && clusters >= 65525) {
        vol->fat_type = 32;
        vol->root_cluster = rootclus ? rootclus : 2;
        return 0;
    }
    return -7;                                   /* FAT16 / odd hybrid */
}

/* ---- FAT chain primitives ---- */

static u64 fat_entry_sector(ocire_fat_vol_t *vol, u32 cluster, u32 *off_out) {
    if (vol->fat_type == 12) {
        u64 off = (u64)cluster + (cluster >> 1);   /* 12 bits each */
        *off_out = (u32)(off % 512);
        return vol->part_lba + vol->fat_start + off / 512;
    }
    u64 off = (u64)cluster * 4;
    *off_out = (u32)(off % 512);
    return vol->part_lba + vol->fat_start + off / 512;
}

/* Returns the next cluster, 0 for free, 0x0FFFFFF8+ for EOC. */
static u32 fat_get_next(ocire_fat_vol_t *vol, u32 cluster) {
    u32 off;
    u64 lba = fat_entry_sector(vol, cluster, &off);
    if (sec_read(vol->ata_idx, lba) != 0) return 0xFFFFFFFF;
    if (vol->fat_type == 12) {
        u32 v = rd16(g_sec + off);
        v = (cluster & 1) ? (v >> 4) : (v & 0xFFF);
        return v >= 0xFF8 ? 0x0FFFFFF8 : v;
    }
    u32 v = rd32(g_sec + off) & 0x0FFFFFFF;
    return v >= 0x0FFFFFF8 ? 0x0FFFFFF8 : v;
}

/* Set one FAT entry (both FAT copies).  value uses EOC 0x0FFFFFF8. */
static int fat_set_next(ocire_fat_vol_t *vol, u32 cluster, u32 value) {
    if (vol->fat_type != 32) return -1;           /* FAT12 is read-only */
    u32 off;
    u64 lba = fat_entry_sector(vol, cluster, &off);
    if (sec_read(vol->ata_idx, lba) != 0) return -1;
    wr32(g_sec + off, value & 0x0FFFFFFF);
    for (u32 i = 0; i < vol->nfats; i++) {
        if (ocire_fat_write_sect(vol, lba + (u64)i * vol->fat_secs, g_sec) != 0)
            return -1;
    }
    return 0;
}

static u64 cluster_lba(ocire_fat_vol_t *vol, u32 cluster) {
    return vol->part_lba + vol->data_start +
           (u64)(cluster - 2) * vol->cluster_secs;
}

u64 ocire_fat_file_first_lba(ocire_fat_vol_t *vol, const ocire_fat_file_t *f) {
    if (f->first_cluster < 2) return 0;
    return cluster_lba(vol, f->first_cluster);
}

/* ---- 8.3 name handling ---- */

/* Convert "OPENCUBE.ELF" (upper, one dot) to the 11-byte on-disk form.
 * Returns 0 on success. */
static int name_to_83(const char *name, u8 out[11]) {
    oc_memset(out, ' ', 11);
    int i = 0;
    for (; name[i] && name[i] != '.' && i < 8; i++) {
        u8 c = (u8)name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i] = c;
    }
    if (name[i] == '.') {
        i++;
        int e = 8;
        for (; name[i] && e < 11; i++, e++) {
            u8 c = (u8)name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[e] = c;
        }
        if (name[i]) return -1;                   /* extension too long */
    }
    if (i == 0) return -1;
    for (int k = 0; k < 11; k++) {
        if (out[k] < 0x20 || out[k] == 0x7F) return -1;
    }
    return 0;
}

static void name_from_83(const u8 e[11], char out[OCIRE_FAT_NAME_LEN]) {
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) out[n++] = (char)e[i];
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) out[n++] = (char)e[i];
    }
    out[n] = 0;
}

/* ---- directory walking ---- */

/* Iterator over the directory starting at cluster (0 = FAT12 root
 * fixed area).  Calls cb(entry_sector, entry_off, dirent_copy) for
 * every live entry; stops on I/O error or when cb returns non-zero. */
typedef int (*dir_iter_fn)(ocire_fat_vol_t *vol, u64 sector, u32 off,
                           const u8 *ent, void *ud);

static int iter_cluster_dir(ocire_fat_vol_t *vol, u32 cluster, dir_iter_fn cb,
                            void *ud) {
    u8 ents[512];
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u64 lba = cluster_lba(vol, cluster);
        for (u32 s = 0; s < vol->cluster_secs; s++) {
            if (ocire_ata_read(vol->ata_idx, lba + s, 1, ents) != 0) return -2;
            for (u32 o = 0; o + DIRENT_SIZE <= 512; o += DIRENT_SIZE) {
                u8 first = ents[o];
                if (first == 0) return 0;             /* end of dir */
                if (first == DELETED) continue;
                if ((ents[o + 11] & ATTR_LFN) == ATTR_LFN) continue;
                int rc = cb(vol, lba + s, o, ents + o, ud);
                if (rc != 0) return rc;
            }
        }
        cluster = fat_get_next(vol, cluster);
    }
    return 0;
}

static int iter_dir(ocire_fat_vol_t *vol, u32 dir_cluster, dir_iter_fn cb,
                    void *ud) {
    if (vol->fat_type == 12 && dir_cluster == 0) {
        /* Fixed root directory area. */
        u8 ents[512];
        u64 root_lba = vol->part_lba + vol->reserved_secs +
                       (u64)vol->nfats * vol->fat_secs;
        for (u64 s = 0; s < vol->root_dir_sectors; s++) {
            if (ocire_ata_read(vol->ata_idx, root_lba + s, 1, ents) != 0)
                return -2;
            for (u32 o = 0; o + DIRENT_SIZE <= 512; o += DIRENT_SIZE) {
                u8 first = ents[o];
                if (first == 0) return 0;
                if (first == DELETED) continue;
                if ((ents[o + 11] & ATTR_LFN) == ATTR_LFN) continue;
                int rc = cb(vol, root_lba + s, o, ents + o, ud);
                if (rc != 0) return rc;
            }
        }
        return 0;
    }
    return iter_cluster_dir(vol, dir_cluster ? dir_cluster : vol->root_cluster,
                            cb, ud);
}

typedef struct {
    const u8 want[11];
    ocire_fat_file_t *out;
    int found;
} lookup_ud_t;

static int lookup_cb(ocire_fat_vol_t *vol, u64 sector, u32 off,
                     const u8 *ent, void *ud) {
    lookup_ud_t *l = (lookup_ud_t *)ud;
    if (oc_memcmp(ent, l->want, 11) != 0) return 0;
    l->out->vol = vol;
    l->out->first_cluster = rd16(ent + 26) | ((u32)rd16(ent + 20) << 16);
    l->out->size = rd32(ent + 28);
    l->out->dir_entry_sector = sector;
    l->out->dir_entry_off = off;
    l->out->data_lba = 0;
    name_from_83(ent, l->out->name);
    l->found = 1;
    return 1;
}

static int lookup_in_dir(ocire_fat_vol_t *vol, u32 dir_cluster,
                         const char *name, ocire_fat_file_t *out) {
    u8 want[11];
    if (name_to_83(name, want) != 0) return -1;
    lookup_ud_t ud;
    oc_memset(&ud, 0, sizeof(ud));
    oc_memcpy((void *)ud.want, want, 11);
    ud.out = out;
    int rc = iter_dir(vol, dir_cluster, lookup_cb, &ud);
    if (rc < 0) return -2;
    return ud.found ? 0 : -1;
}

/* Split "A/B/C" into the directory chain walk + final name. */
int ocire_fat_lookup(ocire_fat_vol_t *vol, const char *path,
                     ocire_fat_file_t *out) {
    oc_memset(out, 0, sizeof(*out));
    u32 dir_cluster = 0;                            /* root */
    char comp[OCIRE_FAT_NAME_LEN];
    const char *p = path;
    while (*p) {
        int n = 0;
        while (*p && *p != '/' && n < OCIRE_FAT_NAME_LEN - 1) {
            comp[n++] = *p++;
        }
        if (n == 0) return -1;
        comp[n] = 0;
        int last = (*p == 0);
        ocire_fat_file_t f;
        int rc = lookup_in_dir(vol, dir_cluster, comp, &f);
        if (rc != 0) return rc;
        if (last) {
            *out = f;
            out->dir_cluster = dir_cluster;
            return 0;
        }
        /* intermediate component must be a directory */
        dir_cluster = f.first_cluster;
        p++;                                        /* skip '/' */
    }
    return -1;
}

/* ---- file reads ---- */

s64 ocire_fat_read_file(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                        u8 *buf, u64 cap) {
    u64 want = f->size;
    if (want > cap) want = cap;
    u64 done = 0;
    u32 cluster = f->first_cluster;
    u8 sec[512];
    while (done < want && cluster >= 2 && cluster < 0x0FFFFFF8) {
        u64 lba = cluster_lba(vol, cluster);
        for (u32 s = 0; s < vol->cluster_secs && done < want; s++) {
            if (ocire_ata_read(vol->ata_idx, lba + s, 1, sec) != 0) return -2;
            u64 take = want - done;
            if (take > 512) take = 512;
            oc_memcpy(buf + done, sec, take);
            done += take;
        }
        cluster = fat_get_next(vol, cluster);
    }
    return (s64)done;
}

/* ---- in-place patch (fixed-size files: mailbox, log) ---- */

int ocire_fat_file_patch(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                         u64 off, const void *buf, u32 len) {
    if (off + len > f->size) return -1;
    u32 cluster = f->first_cluster;
    u8 sec[512];
    u64 pos = 0;                                    /* file byte offset */
    const u8 *src = (const u8 *)buf;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && pos < off + len) {
        u64 clu_bytes = (u64)vol->cluster_secs * 512;
        if (off + len > pos && off < pos + clu_bytes) {
            u64 lba = cluster_lba(vol, cluster);
            for (u32 s = 0; s < vol->cluster_secs; s++) {
                u64 sec_start = pos + (u64)s * 512;
                u64 sec_end = sec_start + 512;
                if (off < sec_end && off + len > sec_start) {
                    if (ocire_ata_read(vol->ata_idx, lba + s, 1, sec) != 0)
                        return -2;
                    u64 from = off > sec_start ? off : sec_start;
                    u64 to = (off + len) < sec_end ? (off + len) : sec_end;
                    oc_memcpy(sec + (from - sec_start), src + (from - off),
                              (u32)(to - from));
                    if (ocire_ata_write(vol->ata_idx, lba + s, 1, sec) != 0)
                        return -3;
                }
            }
        }
        pos += clu_bytes;
        cluster = fat_get_next(vol, cluster);
    }
    return 0;
}

/* ---- FAT32 write set ---- */

/* Find a free cluster.  Returns the cluster number or 0. */
static u32 alloc_cluster(ocire_fat_vol_t *vol) {
    for (u32 c = 2; c < vol->total_clusters + 2; c++) {
        if (fat_get_next(vol, c) == 0) return c;
    }
    return 0;
}

static int free_chain(ocire_fat_vol_t *vol, u32 first) {
    u32 c = first;
    while (c >= 2 && c < 0x0FFFFFF8) {
        u32 next = fat_get_next(vol, c);
        if (fat_set_next(vol, c, 0) != 0) return -1;
        c = next;
    }
    return 0;
}

/* Write the directory entry back (size / first cluster updates). */
static int write_dirent(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                        u32 first_cluster, u64 size) {
    u8 sec[512];
    if (ocire_ata_read(vol->ata_idx, f->dir_entry_sector, 1, sec) != 0)
        return -1;
    u8 *e = sec + f->dir_entry_off;
    wr16(e + 20, (u16)(first_cluster >> 16));
    wr16(e + 26, (u16)(first_cluster & 0xFFFF));
    wr32(e + 28, (u32)size);
    /* Fixed timestamp: 2026-01-01 00:00:00 (reproducible images).
     * FAT date = ((year-1980) << 9) | (month << 5) | day. */
    u16 date = (u16)(((2026 - 1980) << 9) | (1 << 5) | 1);
    wr16(e + 14, 0);                                 /* ctime */
    wr16(e + 16, date);                              /* cdate */
    wr16(e + 18, date);                              /* adate */
    wr16(e + 22, date);                              /* mdate */
    wr16(e + 24, 0);                                 /* mtime */
    return ocire_fat_write_sect(vol, f->dir_entry_sector, sec);
}

/* Write `len` bytes into a FRESH chain (allocates as needed) and
 * terminate it.  Returns the first cluster or 0 on failure (partial
 * allocations are freed). */
static u32 write_new_chain(ocire_fat_vol_t *vol, const u8 *buf, u64 len) {
    u32 first = 0, prev = 0;
    u64 done = 0;
    u8 sec[512];
    while (done < len || first == 0) {
        u32 c = alloc_cluster(vol);
        if (c == 0) {
            if (first) free_chain(vol, first);
            return 0;
        }
        if (prev == 0) first = c;
        else fat_set_next(vol, prev, c);
        /* Fill this cluster. */
        u64 lba = cluster_lba(vol, c);
        for (u32 s = 0; s < vol->cluster_secs; s++) {
            oc_memset(sec, 0, 512);
            u64 take = len - done;
            if (take > 512) take = 512;
            if (take > 0) oc_memcpy(sec, buf + done, take);
            if (ocire_ata_write(vol->ata_idx, lba + s, 1, sec) != 0) {
                free_chain(vol, first);
                return 0;
            }
            done += take;
        }
        prev = c;
    }
    if (prev) fat_set_next(vol, prev, 0x0FFFFFF8);
    return first;
}

int ocire_fat_write_file(ocire_fat_vol_t *vol, const ocire_fat_file_t *f,
                         const u8 *buf, u64 len) {
    if (vol->fat_type != 32) return -1;
    /* Simplest correct path: reallocate the chain for the new length
     * (identical content length or not) and repoint the entry.  For
     * the repair workloads (a handful of system files) the cost is
     * irrelevant and the chain stays unfragmented from a fresh-ish
     * volume state. */
    u32 first = write_new_chain(vol, buf, len);
    if (first == 0) return -2;
    free_chain(vol, f->first_cluster);
    if (write_dirent(vol, f, first, len) != 0) return -3;
    return 0;
}

/* Find a free directory slot in dir (0 = root).  Returns 0 and fills
 * sector/off.  FAT32 only (FAT12 is read-only by design). */
static int find_free_dirent(ocire_fat_vol_t *vol, u32 dir_cluster,
                            u64 *sector, u32 *off) {
    u8 sec[512];
    u32 cluster = dir_cluster ? dir_cluster : vol->root_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u64 lba = cluster_lba(vol, cluster);
        for (u32 s = 0; s < vol->cluster_secs; s++) {
            if (ocire_ata_read(vol->ata_idx, lba + s, 1, sec) != 0) return -1;
            for (u32 o = 0; o + DIRENT_SIZE <= 512; o += DIRENT_SIZE) {
                if (sec[o] == 0 || sec[o] == DELETED) {
                    *sector = lba + s;
                    *off = o;
                    return 0;
                }
            }
        }
        cluster = fat_get_next(vol, cluster);
    }
    /* Root full: extend the chain with a fresh cluster. */
    u32 c = alloc_cluster(vol);
    if (c == 0) return -1;
    u8 fresh[512];
    oc_memset(fresh, 0, 512);
    if (ocire_ata_write(vol->ata_idx, cluster_lba(vol, c), 1, fresh) != 0)
        return -1;
    /* Walk to the end and link. */
    u32 last = dir_cluster ? dir_cluster : vol->root_cluster;
    while (1) {
        u32 n = fat_get_next(vol, last);
        if (n >= 0x0FFFFFF8) break;
        last = n;
    }
    fat_set_next(vol, last, c);
    fat_set_next(vol, c, 0x0FFFFFF8);
    *sector = cluster_lba(vol, c);
    *off = 0;
    return 0;
}

/* Create a directory entry `name` in dir with the given first
 * cluster, attribute and size. */
static int make_dirent(ocire_fat_vol_t *vol, u32 dir_cluster,
                       const char *name, u8 attr, u32 first_cluster,
                       u64 size) {
    u8 want[11];
    if (name_to_83(name, want) != 0) return -1;
    u64 sector;
    u32 off;
    if (find_free_dirent(vol, dir_cluster, &sector, &off) != 0) return -2;
    u8 sec[512];
    if (ocire_ata_read(vol->ata_idx, sector, 1, sec) != 0) return -3;
    u8 *e = sec + off;
    oc_memset(e, 0, DIRENT_SIZE);
    oc_memcpy(e, want, 11);
    e[11] = attr;
    /* NTRes: name is plain uppercase 8.3, no lowercase flags. */
    e[12] = 0;
    wr16(e + 20, (u16)(first_cluster >> 16));
    wr16(e + 26, (u16)(first_cluster & 0xFFFF));
    wr32(e + 28, (u32)size);
    {
        u16 date = (u16)(((2026 - 1980) << 9) | (1 << 5) | 1);
        wr16(e + 14, 0);                             /* ctime */
        wr16(e + 16, date);                          /* cdate */
        wr16(e + 18, date);                          /* adate */
        wr16(e + 22, date);                          /* mdate */
        wr16(e + 24, 0);                             /* mtime */
    }
    return ocire_fat_write_sect(vol, sector, sec);
}

int ocire_fat_create_file(ocire_fat_vol_t *vol, const char *path,
                          const u8 *buf, u64 len) {
    if (vol->fat_type != 32) return -1;
    /* Split parent path / final name. */
    char parent[OCIRE_FAT_NAME_LEN * 8];
    const char *slash = (const char *)0;
    const char *p = path;
    for (; *p; p++) {
        if (*p == '/') slash = p;
    }
    char name[OCIRE_FAT_NAME_LEN];
    u32 parent_cluster = 0;
    if (slash) {
        u64 n = (u64)(slash - path);
        if (n >= sizeof(parent)) return -1;
        oc_memcpy(parent, path, n);
        parent[n] = 0;
        ocire_fat_file_t d;
        if (ocire_fat_lookup(vol, parent, &d) != 0) return -2;
        parent_cluster = d.first_cluster;
        const char *base = slash + 1;
        int i = 0;
        for (; base[i] && i < OCIRE_FAT_NAME_LEN - 1; i++) name[i] = base[i];
        name[i] = 0;
    } else {
        int i = 0;
        for (; path[i] && i < OCIRE_FAT_NAME_LEN - 1; i++) name[i] = path[i];
        name[i] = 0;
    }
    /* Overwrite when it exists (same semantics for repair). */
    ocire_fat_file_t ex;
    if (lookup_in_dir(vol, parent_cluster, name, &ex) == 0) {
        return ocire_fat_write_file(vol, &ex, buf, len);
    }
    u32 first = write_new_chain(vol, buf, len);
    if (first == 0) return -3;
    if (make_dirent(vol, parent_cluster, name, 0x20, first, len) != 0)
        return -4;
    return 0;
}

int ocire_fat_mkdir(ocire_fat_vol_t *vol, const char *path) {
    if (vol->fat_type != 32) return -1;
    ocire_fat_file_t ex;
    if (ocire_fat_lookup(vol, path, &ex) == 0) {
        return 0;                                    /* already there */
    }
    char parent[OCIRE_FAT_NAME_LEN * 8];
    const char *slash = (const char *)0;
    const char *p = path;
    for (; *p; p++) if (*p == '/') slash = p;
    char name[OCIRE_FAT_NAME_LEN];
    u32 parent_cluster = 0;
    if (slash) {
        u64 n = (u64)(slash - path);
        if (n >= sizeof(parent)) return -1;
        oc_memcpy(parent, path, n);
        parent[n] = 0;
        ocire_fat_file_t d;
        if (ocire_fat_lookup(vol, parent, &d) != 0) return -2;
        parent_cluster = d.first_cluster;
        const char *base = slash + 1;
        int i = 0;
        for (; base[i] && i < OCIRE_FAT_NAME_LEN - 1; i++) name[i] = base[i];
        name[i] = 0;
    } else {
        int i = 0;
        for (; path[i] && i < OCIRE_FAT_NAME_LEN - 1; i++) name[i] = path[i];
        name[i] = 0;
    }
    /* Allocate one zeroed cluster for the new directory, then write
     * the "." and ".." entries. */
    u32 c = alloc_cluster(vol);
    if (c == 0) return -3;
    u8 sec[512];
    oc_memset(sec, 0, 512);
    u64 lba = cluster_lba(vol, c);
    for (u32 s = 0; s < vol->cluster_secs; s++) {
        if (ocire_ata_write(vol->ata_idx, lba + s, 1, sec) != 0) return -4;
    }
    fat_set_next(vol, c, 0x0FFFFFF8);
    /* "." entry */
    oc_memset(sec, 0, DIRENT_SIZE);
    oc_memset(sec, ' ', 11);
    sec[0] = '.';
    sec[11] = ATTR_DIR;
    wr16(sec + 20, (u16)(c >> 16));
    wr16(sec + 26, (u16)(c & 0xFFFF));
    /* ".." entry */
    u8 *dot = sec + DIRENT_SIZE;
    oc_memset(dot, 0, DIRENT_SIZE);
    oc_memset(dot, ' ', 11);
    dot[0] = '.'; dot[1] = '.';
    dot[11] = ATTR_DIR;
    wr16(dot + 20, (u16)(parent_cluster >> 16));
    wr16(dot + 26, (u16)(parent_cluster & 0xFFFF));
    if (ocire_ata_write(vol->ata_idx, lba, 1, sec) != 0) return -5;
    if (make_dirent(vol, parent_cluster, name, ATTR_DIR, c, 0) != 0)
        return -6;
    return 0;
}

/* ---- FAT32 format (geometry mirrors the OCOS mkfs.fat32) ---- */

int ocire_fat_format(ocire_fat_vol_t *vol, int ata_idx, u64 part_lba,
                     u64 part_sectors, const char *label) {
    if (part_sectors < 40) return -1;
    u32 total_sect = (u32)part_sectors;
    u32 spc = 1;
    if (total_sect > 532480u) spc = 8;
    if (total_sect > 16384u * 1024u) spc = 16;
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
    oc_memset(buf, 0, 512);
    buf[0] = 0xEB; buf[1] = 0x58; buf[2] = 0x90;
    oc_memcpy(buf + 3, "MSWIN4.1", 8);
    wr16(buf + 11, 512);
    buf[13] = (u8)spc;
    wr16(buf + 14, 32);
    buf[16] = 2;
    wr16(buf + 17, 0);
    wr16(buf + 19, 0);
    buf[21] = 0xF8;
    wr16(buf + 22, 0);
    wr32(buf + 32, total_sect);
    wr32(buf + 36, fat_secs);
    buf[40] = 0x29;                                 /* extended boot sig */
    /* volume id: fixed 0x4F434952 ("OCIR" - repair provenance, and a
     * deterministic image id keeps builds reproducible). */
    wr32(buf + 41, 0x4F434952);
    {
        int i = 0;
        for (; i < 11 && label[i]; i++) {
            u8 c = (u8)label[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            buf[43 + i] = c;
        }
        for (; i < 11; i++) buf[43 + i] = ' ';
    }
    /* FAT32 extended BPB: root cluster 2, fsinfo sector 1, backup
     * boot sector 6 (values as written by the mainstream formatters
     * mkfs.vfat / the OCOS mkfs.fat32 use). */
    wr32(buf + 44, 2);                              /* root cluster = 2 */
    wr16(buf + 48, 1);                              /* fs info sector */
    wr16(buf + 50, 6);                              /* backup boot sector */
    buf[510] = 0x55; buf[511] = 0xAA;
    if (ocire_ata_write(ata_idx, part_lba, 1, buf) != 0) return -2;

    /* Zero the reserved sectors (2..31) - GRUB reads a clean area. */
    oc_memset(buf, 0, 512);
    for (u32 s = 1; s < 32; s++) {
        if (ocire_ata_write(ata_idx, part_lba + s, 1, buf) != 0) return -3;
    }

    /* FAT[0] = media|0xFFFFFF00, FAT[1] = EOC, root cluster chain. */
    for (u32 f = 0; f < 2; f++) {
        u64 fat_lba = part_lba + 32 + (u64)f * fat_secs;
        for (u32 s = 0; s < fat_secs; s++) {
            if (ocire_ata_write(ata_idx, fat_lba + s, 1, buf) != 0) return -4;
        }
        u8 first[512];
        oc_memset(first, 0, 512);
        wr32(first + 0, 0x0FFFFFF8);
        wr32(first + 4, 0x0FFFFFFF);
        wr32(first + 8, 0x0FFFFFFF);                /* cluster 2 = root */
        if (ocire_ata_write(ata_idx, fat_lba, 1, first) != 0) return -5;
    }

    /* Zero the data region head (root directory cluster 2). */
    u64 data_lba = part_lba + 32 + 2ull * fat_secs;
    for (u32 s = 0; s < spc; s++) {
        if (ocire_ata_write(ata_idx, data_lba + s, 1, buf) != 0) return -6;
    }

    return ocire_fat_mount(vol, ata_idx, part_lba);
}
