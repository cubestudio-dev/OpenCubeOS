/* Host-side unit harness for the OCIRE FAT write set (WP-02 debug).
 * Compiles the REAL ocire_fat.c/ocire_sha256.c/ocire_util.c on the
 * host with file-backed ATA stubs and exercises the allocation /
 * chain-build paths that the QEMU battery drives from the shell. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ocire_types.h"
#include "ocire_ata.h"
#include "ocire_util.h"
#include "ocire_fat.h"
#include "ocire_sha256.h"

static FILE *g_img;
static u64 g_writes = 0;

int ocire_ata_scan(void) { return 1; }
int ocire_ata_read(int idx, u64 lba, u32 count, void *buf) {
    (void)idx;
    fseek(g_img, (long)(lba * 512), SEEK_SET);
    size_t r = fread(buf, 512, count, g_img);
    return r == count ? 0 : -1;
}
int ocire_ata_write(int idx, u64 lba, u32 count, const void *buf) {
    (void)idx;
    g_writes += count;
    fseek(g_img, (long)(lba * 512), SEEK_SET);
    size_t r = fwrite(buf, 512, count, g_img);
    return r == count ? 0 : -1;
}

/* python chain walker as a C sanity check */
static u64 chain_len(ocire_fat_vol_t *v, u32 first) {
    u64 n = 0;
    u32 c = first;
    while (c >= 2 && c < 0x0FFFFFF7) {
        u8 sec[512];
        u32 off;
        u64 lba = v->part_lba + v->fat_start; /* recompute below */
        (void)lba;
        /* read FAT entry directly */
        u64 fat_off = (u64)c * 4;
        u64 fat_lba = v->part_lba + v->reserved_secs + fat_off / 512;
        u32 ent_off = (u32)(fat_off % 512);
        if (ocire_ata_read(v->ata_idx, fat_lba, 1, sec) != 0) return ~0ull;
        u32 e = (u32)(sec[ent_off] | (sec[ent_off+1] << 8) |
                      (sec[ent_off+2] << 16) | ((u32)sec[ent_off+3] << 24))
                & 0x0FFFFFFF;
        if (e == 0) { printf("    chain hits FREE entry at %u\n", c); return n; }
        if (e == 0x0FFFFFF7 && n > 0) { printf("    chain hits MARKER at %u\n", c); return n; }
        n++;
        c = e;
    }
    return n;
}

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  [PASS] %s\n", name); \
    else { printf("  [FAIL] %s\n", name); failures++; } \
} while (0)

/* deterministic pattern */
static void fill_pattern(u8 *buf, u64 len, u64 seed) {
    u64 x = seed * 2654435761u + 12345;
    for (u64 i = 0; i < len; i++) {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        buf[i] = (u8)(x >> 56);
    }
}

static int digest_of(ocire_fat_vol_t *v, const char *path, char hex[65],
                     u64 *size_out) {
    ocire_fat_file_t f;
    if (ocire_fat_lookup(v, path, &f) != 0) return -1;
    u8 d[32];
    if (ocire_fat_hash_file(v, &f, d, size_out) != 0) return -2;
    ocire_sha256_hex(d, hex);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: fattest <fat32-image>\n"); return 2; }
    g_img = fopen(argv[1], "r+b");
    if (!g_img) { perror("open"); return 2; }

    ocire_fat_vol_t vol;
    if (ocire_fat_mount(&vol, 0, 0) != 0) { printf("mount FAILED\n"); return 1; }
    printf("mounted: fat_type=%u cluster=%u bytes total_clusters=%u\n",
           vol.fat_type, vol.cluster_secs * 512, vol.total_clusters);

    /* ---- 1. create_file with a big patterned file (2070 clusters) ---- */
    {
        static u8 big[2118992];
        fill_pattern(big, sizeof(big), 7);
        u8 d[32]; char want[65];
        ocire_sha256(big, sizeof(big), d);
        ocire_sha256_hex(d, want);
        int rc = ocire_fat_create_file(&vol, "TESTBIG.BIN", big, sizeof(big));
        CHECK(rc == 0, "create_file TESTBIG.BIN (2070 clusters)");
        char got[65]; u64 sz = 0;
        CHECK(digest_of(&vol, "TESTBIG.BIN", got, &sz) == 0 &&
              strcmp(got, want) == 0 && sz == sizeof(big),
              "TESTBIG.BIN digest+size round-trip");
        ocire_fat_file_t f;
        ocire_fat_lookup(&vol, "TESTBIG.BIN", &f);
        u64 cl = chain_len(&vol, f.first_cluster);
        u64 need = (sizeof(big) + (u64)vol.cluster_secs * 512 - 1) /
                   ((u64)vol.cluster_secs * 512);
        printf("    chain=%llu need=%llu\n", (unsigned long long)cl,
               (unsigned long long)need);
        CHECK(cl == need, "TESTBIG.BIN chain length exact");
    }

    /* ---- 2. overwrite via write_file (different size, same name) ---- */
    {
        ocire_fat_file_t f;
        CHECK(ocire_fat_lookup(&vol, "TESTBIG.BIN", &f) == 0, "lookup before overwrite");
        static u8 alt[1500000];          /* not a cluster multiple */
        fill_pattern(alt, sizeof(alt), 99);
        u8 d[32]; char want[65];
        ocire_sha256(alt, sizeof(alt), d);
        ocire_sha256_hex(d, want);
        int rc = ocire_fat_write_file(&vol, &f, alt, sizeof(alt));
        CHECK(rc == 0, "write_file overwrite (1465 clusters)");
        char got[65]; u64 sz = 0;
        CHECK(digest_of(&vol, "TESTBIG.BIN", got, &sz) == 0 &&
              strcmp(got, want) == 0 && sz == sizeof(alt),
              "overwrite digest+size round-trip");
        ocire_fat_lookup(&vol, "TESTBIG.BIN", &f);
        u64 cl = chain_len(&vol, f.first_cluster);
        u64 need = (sizeof(alt) + 1023) / 1024;
        printf("    chain=%llu need=%llu\n", (unsigned long long)cl,
               (unsigned long long)need);
        CHECK(cl == need, "overwrite chain length exact");
    }

    /* ---- 3. copy_stream into a NEW path (FAT32 -> FAT32) ---- */
    {
        ocire_fat_file_t src;
        CHECK(ocire_fat_lookup(&vol, "TESTBIG.BIN", &src) == 0, "copy src lookup");
        static u8 chunk[8192];
        u64 written = 0;
        int rc = ocire_fat_copy_stream(&vol, &src, &vol, "COPY.TXT",
                                       chunk, sizeof(chunk), &written);
        CHECK(rc == 0 && written == src.size, "copy_stream new path");
        char a[65], b[65]; u64 sa = 0, sb = 0;
        CHECK(digest_of(&vol, "TESTBIG.BIN", a, &sa) == 0 &&
              digest_of(&vol, "COPY.TXT", b, &sb) == 0 &&
              strcmp(a, b) == 0 && sa == sb, "copy digest identical");
        ocire_fat_file_t f;
        ocire_fat_lookup(&vol, "COPY.TXT", &f);
        u64 cl = chain_len(&vol, f.first_cluster);
        u64 need = (src.size + 1023) / 1024;
        printf("    chain=%llu need=%llu\n", (unsigned long long)cl,
               (unsigned long long)need);
        CHECK(cl == need, "copy chain length exact");
    }

    /* ---- 4. copy_stream OVER an existing path (have_ex branch) ---- */
    {
        ocire_fat_file_t src;
        ocire_fat_lookup(&vol, "TESTBIG.BIN", &src);
        static u8 chunk[8192];
        u64 written = 0;
        int rc = ocire_fat_copy_stream(&vol, &src, &vol, "COPY.TXT",
                                       chunk, sizeof(chunk), &written);
        CHECK(rc == 0 && written == src.size, "copy_stream overwrite path");
        char a[65], b[65]; u64 sa = 0, sb = 0;
        CHECK(digest_of(&vol, "TESTBIG.BIN", a, &sa) == 0 &&
              digest_of(&vol, "COPY.TXT", b, &sb) == 0 &&
              strcmp(a, b) == 0 && sa == sb, "overwrite-copy digest identical");
        /* the OLD chain must be freed: walk COPY.TXT then count free space
         * indirectly - at least ensure no marker/free entry mid-chain */
        ocire_fat_file_t f;
        ocire_fat_lookup(&vol, "COPY.TXT", &f);
        u64 cl = chain_len(&vol, f.first_cluster);
        u64 need = (src.size + 1023) / 1024;
        CHECK(cl == need, "overwrite-copy chain length exact");
    }

    /* ---- 5. boundary sizes: exact cluster multiple, 1 byte, 1.5 clusters ---- */
    {
        static u8 b0[1024 * 100];   /* exact multiple of 1024 */
        static u8 b1[1];
        static u8 b2[1536];         /* 1.5 clusters */
        struct { const char *name; u8 *buf; u64 len; } cases[] = {
            { "EDGEEXAC.BIN", b0, sizeof(b0) },
            { "EDGEONE.BIN",  b1, sizeof(b1) },
            { "EDGEHALF.BIN", b2, sizeof(b2) },
        };
        for (unsigned i = 0; i < 3; i++) {
            fill_pattern(cases[i].buf, cases[i].len, 31 + i);
            u8 d[32]; char want[65]; char got[65]; u64 sz = 0;
            ocire_sha256(cases[i].buf, cases[i].len, d);
            ocire_sha256_hex(d, want);
            int rc = ocire_fat_create_file(&vol, cases[i].name,
                                           cases[i].buf, cases[i].len);
            char label[96];
            snprintf(label, sizeof(label), "edge create %s (%llu bytes)",
                     cases[i].name, (unsigned long long)cases[i].len);
            CHECK(rc == 0, label);
            snprintf(label, sizeof(label), "edge digest %s", cases[i].name);
            CHECK(digest_of(&vol, cases[i].name, got, &sz) == 0 &&
                  strcmp(got, want) == 0 && sz == cases[i].len, label);
            ocire_fat_file_t f;
            ocire_fat_lookup(&vol, cases[i].name, &f);
            u64 cl = chain_len(&vol, f.first_cluster);
            u64 need = (cases[i].len + 1023) / 1024;
            snprintf(label, sizeof(label), "edge chain %s (%llu==%llu)",
                     cases[i].name, (unsigned long long)cl,
                     (unsigned long long)need);
            CHECK(cl == need, label);
        }
    }

    /* ---- 6. names that do not fit 8.3 must be REJECTED, not truncated ---- */
    {
        static u8 buf[2048];
        fill_pattern(buf, sizeof(buf), 77);
        int rc_c = ocire_fat_create_file(&vol, "TOOLONGNAME.BIN", buf,
                                         sizeof(buf));
        CHECK(rc_c != 0, "create over-long name rejected");
        ocire_fat_file_t f;
        CHECK(ocire_fat_lookup(&vol, "TOOLONGNAME.BIN", &f) != 0,
              "lookup over-long name rejected");
        CHECK(ocire_fat_lookup(&vol, "TOOLONGNA", &f) != 0 ||
              (f.size == 0 && f.first_cluster < 2),
              "no truncated ghost entry left behind");
        CHECK(ocire_fat_mkdir(&vol, "TOOLONGDIR.NAME") != 0,
              "mkdir over-long name rejected");
        ocire_fat_file_t src;
        ocire_fat_lookup(&vol, "EDGEHALF.BIN", &src);
        static u8 chunk[8192];
        u64 wr = 0;
        CHECK(ocire_fat_copy_stream(&vol, &src, &vol, "BOOT/TOOLONGNAME.X",
                                    chunk, sizeof(chunk), &wr) != 0,
              "copy_stream over-long name rejected");
    }

    /* ---- 7. mkdir + create inside it ---- */
    {
        CHECK(ocire_fat_mkdir(&vol, "NEWDIR") == 0, "mkdir NEWDIR");
        static u8 small[500];
        fill_pattern(small, sizeof(small), 5);
        u8 d[32]; char want[65]; char got[65]; u64 sz = 0;
        ocire_sha256(small, sizeof(small), d);
        ocire_sha256_hex(d, want);
        CHECK(ocire_fat_create_file(&vol, "NEWDIR/SMALL.BIN", small,
                                    sizeof(small)) == 0,
              "create NEWDIR/SMALL.BIN");
        CHECK(digest_of(&vol, "NEWDIR/SMALL.BIN", got, &sz) == 0 &&
              strcmp(got, want) == 0 && sz == sizeof(small),
              "subdir file digest");
    }

    /* ---- 7. read_range offsets (the streaming read path) ---- */
    {
        ocire_fat_file_t f;
        ocire_fat_lookup(&vol, "TESTBIG.BIN", &f);
        static u8 probe[4096];
        s64 got = ocire_fat_read_range(&vol, &f, 12345, probe, sizeof(probe));
        ocire_fat_file_t g;
        /* compare with a direct re-read of the same source bytes */
        static u8 whole[2118992];
        (void)g;
        CHECK(got == 4096, "read_range length at offset 12345");
        /* verify content via full hash of a fresh create - reuse pattern 99 */
        static u8 alt[1500000];
        fill_pattern(alt, sizeof(alt), 99);
        CHECK(memcmp(probe, alt + 12345, 4096) == 0,
              "read_range content matches source pattern");
    }

    printf("\n%s (%llu sector writes)\n", failures ? "FAILURES PRESENT" : "ALL PASS",
           (unsigned long long)g_writes);
    fclose(g_img);
    return failures ? 1 : 0;
}
