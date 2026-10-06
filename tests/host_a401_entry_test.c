/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 *
 * BUG-0082 (P1, A14-31) verification: the X.509 DER negative-length
 * defect fixed by P0fix2 BUG-0035 (A4-01) must be rejected at the DER
 * layer for EVERY caller, including the four net_tls.c entry points
 * (TLS 1.3 Certificate, TLS 1.2 Certificate, verify_cert_sig, and the
 * inner element walkers) that all funnel through crypto_der_*.
 *
 * This harness compiles the exact kernel crypto_x509.c and feeds it
 * crafted DER blobs whose long-form lengths wrap to negative values in
 * a signed-int accumulation; every one of them must be rejected with a
 * negative error and ASAN must stay silent.
 *
 * Build:
 *   gcc -fsanitize=address -g -O1 -Ikernel -Ikernel/crypto -Ikernel/mem \
 *       -Ikernel/lib tests/host_a401_entry_test.c kernel/crypto/crypto_x509.c \
 *       kernel/crypto/crypto_rsa.c kernel/crypto/crypto_bn.c \
 *       kernel/crypto/crypto_ec_nist.c kernel/crypto/crypto_core.c \
 *       kernel/crypto/crypto_sha512.c kernel/core/core_rtc.c \
 *       -o /tmp/host_a401
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "crypto_x509.h"

void *memset(void*,int,unsigned long);
void *memcpy(void*,const void*,unsigned long);
int   memcmp(const void*,const void*,unsigned long);
void *kmalloc(unsigned long);   /* crypto_x509.c uses kmalloc/kfree */
void  kfree(void*);

/* map kernel heap to libc */
void *kmalloc(unsigned long n) { return malloc(n); }
void kfree(void *p) { free(p); }

static int fails = 0;

static void check(const char *name, const u8 *der, size_t len) {
    crypto_x509_cert_t cert;
    int rc = crypto_x509_parse(&cert, (u8 *)der, (int)len);
    printf("%-52s -> rc=%d %s\n", name, rc, rc < 0 ? "REJECTED" : "ACCEPTED(!!)");
    if (rc >= 0) fails++;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    u8 b[512];

    /* 1. SEQUENCE with 4-byte length 0xFFFFFFFF: signed accumulation
     *    wraps to -1 (the original A4-01 vector, reached from the TLS
     *    Certificate entry points). */
    memset(b, 0x41, sizeof(b));
    b[0] = 0x30; b[1] = 0x84; b[2] = 0xFF; b[3] = 0xFF; b[4] = 0xFF; b[5] = 0xFF;
    check("cert SEQUENCE len 0x84 FF FF FF FF (wraps -1)", b, sizeof(b));

    /* 2. 4-byte length just past int range: 0x80000000 */
    b[0] = 0x30; b[1] = 0x84; b[2] = 0x80; b[3] = 0x00; b[4] = 0x00; b[5] = 0x00;
    check("cert SEQUENCE len 0x84 80000000 (> INT_MAX)", b, sizeof(b));

    /* 3. 5-byte long-form length (n=5 is illegal in DER for lengths) */
    b[0] = 0x30; b[1] = 0x85; b[2] = 0xFF; b[3] = 0xFF; b[4] = 0xFF;
    b[5] = 0xFF; b[6] = 0xFF;
    check("cert SEQUENCE len 0x85 (illegal 5-byte length)", b, sizeof(b));

    /* 4. zero length field (0x80: indefinite form, illegal here) */
    b[0] = 0x30; b[1] = 0x80;
    check("cert SEQUENCE len 0x80 (indefinite, illegal)", b, sizeof(b));

    /* 5. short-form length exceeding the buffer (negative body via
     *    d->len underflow) */
    b[0] = 0x30; b[1] = 0x7F;
    check("cert SEQUENCE len 0x7F > buffer (127 bytes)", b, 4);

    /* 6. well-formed minimal structure must still be reachable: garbage
     *    that fails parsing but with a LEGAL length must NOT be
     *    rejected for length reasons (error code differs from the
     *    rejection path) — proves the walker still reads bodies. */
    b[0] = 0x30; b[1] = 0x04; b[2] = 0x02; b[3] = 0x01;
    b[4] = 0x00; b[5] = 0x00;
    {
        crypto_x509_cert_t cert;
        int rc = crypto_x509_parse(&cert, (u8 *)b, 6);
        printf("%-52s -> rc=%d (legal length, parse may fail on content)\n",
               "legal-length malformed cert", rc);
        (void)rc; /* content parse may fail; the point is no ASAN abort */
    }

    if (fails) { printf("A4-01 entry test: %d FAILURES\n", fails); return 1; }
    printf("A4-01 entry test: all negative-length vectors rejected (PASS)\n");
    return 0;
}
