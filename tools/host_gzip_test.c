/* SPDX-License-Identifier: Apache-2.0 */
/* Host-side debug harness for kernel/lib/lib_gzip.c + kernel/lib/lib_tar.c.
 *
 * BUG-0303 (A5-14) repair history: the WP-10 project restructure moved
 * kernel/gzip.c to kernel/lib/lib_gzip.c and kernel/tar.c to
 * kernel/lib/lib_tar.c, so this harness #include-ed files that no longer
 * existed (the WP-10u gzip/tar regression was dead since then). The
 * restructure script had also mechanically renamed the old correct
 * oc_memset->libc forwarding shims into `memset` CALLING ITSELF - guaranteed
 * infinite recursion even once the include paths were fixed. That class of
 * libc-shadowing shim is gone entirely: glibc provides memset/memcpy/strcmp/
 * ... and the kernel sources bind to it (usize == size_t on this platform).
 * The strncpy shim that forced d[n-1] = 0 (masking real kernel strncpy
 * semantics, see A5-08/A5-14) is gone with it.
 *
 * NOTE: do NOT include <string.h> in this TU - lib_string.h (pulled in by
 * the kernel sources) declares strchr with a kernel signature that
 * conflicts with glibc's, which hard-errors the build.
 *
 * Build (from oc-os/):
 *   gcc -O2 -I. -Isrc/L0/core/lib -Isrc/L0/core/mem tools/host_gzip_test.c \
 *       -o /tmp/host_gzip_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "src/L0/core/types.h"

/* host kmalloc/kfree shims (kernel sources link against mem_heap) */
void *kmalloc(u64 n) { return malloc(n); }
void kfree(void *p) { free(p); }

static int run_one(const char *name, const u8 *gz, int len, const char *expect, int explen);
static void lib_tar_selftest(void);

#include "update_test_vectors.h"

/* include the kernel sources directly (glibc supplies the libc calls) */
#include "src/L0/core/lib/lib_gzip.c"
#include "src/L0/core/lib/lib_tar.c"

static int run_one(const char *name, const u8 *gz, int len,
                   const char *expect, int explen) {
    static u8 out[70000];
    int n = gunzip(gz, len, out, (int)sizeof(out));
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

static int lib_tar_acc_cb(void *ctx, const char *path, int type, u64 size,
                      const u8 *data, int len) {
    (void)ctx;
    if (type == OC_TAR_DIR) { printf("  [tar] dir %s\n", path); return 0; }
    if (data == NULL && len == 0) { printf("  [tar] end of %s\n", path); return 0; }
    printf("  [tar] file %s size=%llu slice=%d: %.20s%s\n", path,
           (unsigned long long)size, len, data ? (const char *)data : "",
           len > 20 ? "..." : "");
    return 0;
}

static void lib_tar_selftest(void) {
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

    lib_tar_t *t = lib_tar_open(lib_tar_acc_cb, NULL);
    int rc = 0, pos = 0, total = (int)sizeof(blk);
    while (pos < total) {
        int take = 100; if (pos + take > total) take = total - pos;
        rc = lib_tar_feed(t, blk + pos, take);
        if (rc != 0) break;
        pos += take;
    }
    if (rc == 0) rc = lib_tar_finish(t);
    printf("tar parse rc=%d\n", rc);
    lib_tar_close(t);
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
        int n = gunzip(bcrc, (int)sizeof(bcrc), out, (int)sizeof(out));
        printf("%-28s n=%d (exp %d) %s\n", "corrupt-crc", n, OC_GZIP_ERR_CRC,
               n == OC_GZIP_ERR_CRC ? "OK" : "FAIL");
        if (n != OC_GZIP_ERR_CRC) fails++;
    }
    {
        static u8 out[64];
        int n = gunzip(GZV_TRUNC_GZ, (int)sizeof(GZV_TRUNC_GZ), out,
                          (int)sizeof(out));
        printf("%-28s n=%d (exp <0) %s\n", "truncated", n,
               n < 0 ? "OK" : "FAIL");
        if (n >= 0) fails++;
    }
    lib_tar_selftest();
    printf("fails=%d\n", fails);
    return fails != 0;
}
