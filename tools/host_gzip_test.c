/* SPDX-License-Identifier: Apache-2.0 */
/* Host-side debug harness for kernel/gzip.c + kernel/tar.c (WP-10u). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "kernel/types.h"

/* host kmalloc shim */
static void *kmalloc(u64 n) { return malloc(n); }
static void kfree(void *p) { free(p); }

/* oc_* string shims (kernel/string.h declares them) */
void *oc_memset(void *d, int c, usize n) { return memset(d, c, n); }
void *oc_memcpy(void *d, const void *s, usize n) { return memcpy(d, s, n); }
int   oc_memcmp(const void *a, const void *b, usize n) { return memcmp(a, b, n); }
usize oc_strlen(const char *s) { return strlen(s); }
int   oc_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int   oc_strncmp(const char *a, const char *b, usize n) { return strncmp(a, b, n); }
char *oc_strcpy(char *d, const char *s) { return strcpy(d, s); }
char *oc_strncpy(char *d, const char *s, usize n) {
    strncpy(d, s, n);
    if (n > 0) d[n - 1] = 0;
    return d;
}
usize oc_u64_to_str(u64 v, char *out) { sprintf(out, "%llu", (unsigned long long)v); return strlen(out); }
static int run_one(const char *name, const u8 *gz, int len, const char *expect, int explen);
static void tar_selftest(void);

#include "update_test_vectors.h"

/* include the kernel sources directly (they only rely on oc_* shims) */
#include "kernel/gzip.c"
#include "kernel/tar.c"

static int run_one(const char *name, const u8 *gz, int len,
                   const char *expect, int explen) {
    static u8 out[70000];
    int n = oc_gunzip(gz, len, out, (int)sizeof(out));
    int ok = (n == explen) && (memcmp(out, expect, explen) == 0);
    printf("%-28s n=%d (exp %d) %s", name, n, explen, ok ? "OK" : "FAIL");
    if (!ok && n >= 0) {
        for (int i = 0; i < (n < explen ? n : explen); i++) {
            if (out[i] != (u8)expect[i]) {
                printf(" first-diff@%d got=%02x exp=%02x", i, out[i], (u8)expect[i]);
                break;
            }
        }
    }
    printf("\n");
    return ok;
}

static int tar_acc_cb(void *ctx, const char *path, int type, u64 size,
                      const u8 *data, int len) {
    (void)ctx;
    if (type == OC_TAR_DIR) { printf("  [tar] dir %s\n", path); return 0; }
    if (data == NULL && len == 0) { printf("  [tar] end of %s\n", path); return 0; }
    printf("  [tar] file %s size=%llu slice=%d: %.20s%s\n", path,
           (unsigned long long)size, len, data ? (const char *)data : "",
           len > 20 ? "..." : "");
    return 0;
}

static void tar_selftest(void) {
    static u8 blk[512 * 5];
    memset(blk, 0, sizeof(blk));
    /* build a header by hand (checksum done manually) */
    strcpy((char *)blk, "etc/opencube.conf");
    memcpy(blk + 100, "0000644\0", 8);
    memcpy(blk + 124, "00000000013\0", 12);
    memcpy(blk + 136, "00000000000\0", 12);
    memset(blk + 148, ' ', 8);
    blk[156] = '0';
    memcpy(blk + 257, "ustar\0", 6);
    memcpy(blk + 263, "00", 2);
    u32 sum = 0;
    for (int i = 0; i < 512; i++) sum += blk[i];
    char cs[9];
    snprintf(cs, sizeof(cs), "%07o", sum);
    memcpy(blk + 148, cs, 8);
    memcpy(blk + 512, "HELLO-TAR!\0", 11);

    oc_tar_t *t = oc_tar_open(tar_acc_cb, NULL);
    int rc = 0, pos = 0, total = (int)sizeof(blk);
    while (pos < total) {
        int take = 100; if (pos + take > total) take = total - pos;
        rc = oc_tar_feed(t, blk + pos, take);
        if (rc != 0) break;
        pos += take;
    }
    if (rc == 0) rc = oc_tar_finish(t);
    printf("tar parse rc=%d\n", rc);
    oc_tar_close(t);
}

int main(void) {
    int fails = 0;
    fails += !run_one("stored", GZV_stored_GZ, (int)sizeof(GZV_stored_GZ),
                      GZV_PAYLOAD_EXPECTED, GZV_PAYLOAD_LEN);
    fails += !run_one("fixed", GZV_fixed_GZ, (int)sizeof(GZV_fixed_GZ),
                      GZV_PAYLOAD_EXPECTED, GZV_PAYLOAD_LEN);
    fails += !run_one("dynamic", GZV_dynamic_GZ, (int)sizeof(GZV_dynamic_GZ),
                      GZV_PAYLOAD_EXPECTED, GZV_PAYLOAD_LEN);
    fails += !run_one("tiny", GZV_tiny_GZ, (int)sizeof(GZV_tiny_GZ),
                      "ABCDEF", 6);
    fails += !run_one("fname", GZV_fname_GZ, (int)sizeof(GZV_fname_GZ),
                      GZV_PAYLOAD_EXPECTED, GZV_PAYLOAD_LEN);
    fails += !run_one("fextra", GZV_fextra_GZ, (int)sizeof(GZV_fextra_GZ),
                      GZV_PAYLOAD_EXPECTED, GZV_PAYLOAD_LEN);
    fails += !run_one("both", GZV_both_GZ, (int)sizeof(GZV_both_GZ),
                      GZV_PAYLOAD_EXPECTED, GZV_PAYLOAD_LEN);
    {
        static u8 bcrc[sizeof(GZV_dynamic_GZ)];
        memcpy(bcrc, GZV_dynamic_GZ, sizeof(GZV_dynamic_GZ));
        bcrc[sizeof(bcrc) - 5] ^= 0xFF;
        static u8 out[8192];
        int n = oc_gunzip(bcrc, (int)sizeof(bcrc), out, (int)sizeof(out));
        printf("%-28s n=%d (exp %d) %s\n", "corrupt-crc", n, OC_GZIP_ERR_CRC,
               n == OC_GZIP_ERR_CRC ? "OK" : "FAIL");
        if (n != OC_GZIP_ERR_CRC) fails++;
    }
    {
        static u8 out[64];
        int n = oc_gunzip(GZV_TRUNC_GZ, (int)sizeof(GZV_TRUNC_GZ), out,
                          (int)sizeof(out));
        printf("%-28s n=%d (exp <0) %s\n", "truncated", n,
               n < 0 ? "OK" : "FAIL");
        if (n >= 0) fails++;
    }
    tar_selftest();
    printf("fails=%d\n", fails);
    return fails != 0;
}
