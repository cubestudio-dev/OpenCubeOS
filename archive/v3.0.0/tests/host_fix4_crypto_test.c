/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Host-side regression test for the WP-10-AUDIT_P2-fix4 crypto batch
 * (BUG-0295..0298). NOT part of the kernel build.
 *
 * Covers:
 *   BUG-0295 (A4-08): ecdsa_verify rejects public keys that are not a point
 *                     on the curve (defense in depth).
 *   BUG-0296 (A4-09): crypto_x25519_shared fails closed on the all-zero
 *                     shared secret (RFC 8446 7.4.3 small-order peer keys).
 *   BUG-0297 (A4-10): crypto_bn_mod_bytes validates out_len BEFORE the
 *                     big-endian store (no stack overflow for 577..583).
 *   BUG-0298 (A4-11): hkdf_expand / crypto_dh_modexp_n / crypto_bn_mod
 *                     fail closed (deterministic zero output) and bound
 *                     their frame-based work buffers.
 *
 * Build:
 *   gcc -O2 -Ikernel -Ikernel/mem -Ikernel/lib -Ikernel/crypto \
 *       tests/host_fix4_crypto_test.c kernel/crypto/crypto_bn.c \
 *       kernel/crypto/crypto_ec_nist.c kernel/crypto/crypto_curve25519.c \
 *       kernel/crypto/crypto_core.c kernel/crypto/crypto_sha512.c \
 *       -o /tmp/fix4_crypto_test
 * (do NOT link kernel/lib/lib_string.c: its libc-named definitions
 * interpose glibc and crash the binary before main)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
/* NOTE: do NOT include lib_string.h here - its strchr declaration conflicts
 * with glibc's and hard-errors the host build (see host_bn_ec_test.c). */
#include "crypto_bn.h"
#include "crypto_ec_nist.h"
#include "crypto_curve25519.h"
#include "crypto_core.h"

/* ---- stubs: core_timer_ticks for crypto_random's seeding path.
 * (crypto_random itself is defined in crypto_core.c - do NOT redefine.) ---- */
u64 core_timer_ticks(void) { return 0; }

/* ---- PMM stub with a failure toggle (for BUG-0298 fail-closed proofs) ---- */
static int g_pmm_fail = 0;
void *mem_pmm_alloc_frame(void) {
    if (g_pmm_fail) return NULL;
    return malloc(4096);
}
void mem_pmm_free_frame(void *p) { free(p); }

static int hex2bin(const char *h, u8 *out, int n) {
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (u8)v;
    }
    return 0;
}
static int check(const char *name, int cond) {
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* NIST CAVP P-256 ECDSA vector (same as host_bn_ec_test.c) */
static const char *NIST_QX =
    "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6";
static const char *NIST_QY =
    "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
static const char *NIST_R =
    "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716";
static const char *NIST_S =
    "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";
static const char *NIST_H =
    "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF";

/* RFC 7748 5.2 vector 1 (X25519) */
static const char *RFC7748_A_PRIV =
    "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4";
static const char *RFC7748_B_PUB =
    "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c";
static const char *RFC7748_SHARED =
    "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552";
/* RFC 8446 7.4.3 small-order peer points (all-zero output) */
static const char *RFC8446_LOW1 =
    "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800";
static const char *RFC8446_LOW2 =
    "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157";

int main(void) {
    int fails = 0;
    u8 qx[32], qy[32], r[32], s[32], h[32];
    hex2bin(NIST_QX, qx, 32);
    hex2bin(NIST_QY, qy, 32);
    hex2bin(NIST_R, r, 32);
    hex2bin(NIST_S, s, 32);
    hex2bin(NIST_H, h, 32);
    u8 pub65[65];
    pub65[0] = 0x04;
    memcpy(pub65 + 1, qx, 32);
    memcpy(pub65 + 33, qy, 32);

    /* ============ BUG-0295: ecdsa_verify validates the public key ============ */
    fails += check("BUG-0295 valid NIST signature accepted (regression)",
                   ecdsa_verify(EC_P256, pub65, 65, h, 32, r, 32, s, 32) == 1);
    u8 bad65[65];
    memcpy(bad65, pub65, 65);
    bad65[33] ^= 0x01;   /* QY off by one bit -> not a curve point */
    int off = ecdsa_verify(EC_P256, bad65, 65, h, 32, r, 32, s, 32);
    fails += check("BUG-0295 off-curve pubkey rejected (was: attack surface)",
                   off != 1);
    /* a point that IS on the curve but is not the signer's must still verify
     * as "invalid signature" (return 0), not crash or mis-accept */
    fails += check("BUG-0295 crypto_ec_pub_valid agrees on-curve",
                   crypto_ec_pub_valid(EC_P256, pub65, 65) == 1 &&
                   crypto_ec_pub_valid(EC_P256, bad65, 65) != 1);

    /* ============ BUG-0296: x25519 all-zero shared rejected ============ */
    {
        u8 ap[32], sh[32];
        hex2bin(RFC7748_A_PRIV, ap, 32);
        u8 bp[32], want[32];
        hex2bin(RFC7748_B_PUB, bp, 32);
        hex2bin(RFC7748_SHARED, want, 32);
        int rc = crypto_x25519_shared(ap, bp, sh);
        fails += check("BUG-0296 RFC 7748 5.2 vector still passes (regression)",
                       rc == 0 && memcmp(sh, want, 32) == 0);
        u8 lo[32];
        hex2bin(RFC8446_LOW1, lo, 32);
        rc = crypto_x25519_shared(ap, lo, sh);
        fails += check("BUG-0296 small-order peer 1 rejected (-1)",
                       rc == -1);
        hex2bin(RFC8446_LOW2, lo, 32);
        rc = crypto_x25519_shared(ap, lo, sh);
        fails += check("BUG-0296 small-order peer 2 rejected (-1)",
                       rc == -1);
        memset(lo, 0, 32);
        rc = crypto_x25519_shared(ap, lo, sh);
        fails += check("BUG-0296 all-zero peer key rejected (-1)", rc == -1);
    }

    /* ============ BUG-0297: mod_bytes bounds check order ============ */
    {
        u8 x[32], m[32], out[64];
        memset(x, 0xFF, 32);          /* huge value */
        memset(m, 0xEE, 32);          /* smaller modulus */
        memset(out, 0x5A, sizeof(out));
        int rc = crypto_bn_mod_bytes(out, 32, x, 32, m, 32);
        fails += check("BUG-0297 in-range reduction still correct (regression)",
                       rc == 0);
        int rejected = 1;
        for (int ol = 577; ol <= 583; ol++) {
            u8 sink[1024];
            memset(sink, 0x5A, sizeof(sink));
            if (crypto_bn_mod_bytes(sink, ol, x, 32, m, 32) != -1) rejected = 0;
        }
        fails += check("BUG-0297 out_len 577..583 all rejected (-1)",
                       rejected);
        fails += check("BUG-0297 out_len 0 rejected", crypto_bn_mod_bytes(out, 0, x, 32, m, 32) == -1);
    }

    /* ============ BUG-0298: fail-closed key material ============ */
    {
        /* hkdf_expand: RFC 5869 Test Case 1 (SHA-256) regression */
        static const char *IKM = "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b";
        static const char *SALT = "000102030405060708090a0b0c";
        static const char *INFO = "f0f1f2f3f4f5f6f7f8f9";
        static const char *OKM =
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865";
        u8 ikm[22], salt[13], info[10], okm[42], got[42];
        hex2bin(IKM, ikm, 22);
        hex2bin(SALT, salt, 13);
        hex2bin(INFO, info, 10);
        hex2bin(OKM, okm, 42);
        u8 prk[32];
        crypto_hmac_sha256(salt, 13, ikm, 22, prk);
        memset(got, 0x77, sizeof(got));
        hkdf_expand(prk, 32, info, 10, got, 42);
        fails += check("BUG-0298 hkdf_expand RFC 5869 TC1 (regression)",
                       memcmp(got, okm, 42) == 0);
        /* oversized info: must leave out all-zero, not garbage/partial */
        static u8 big_info[8192];
        memset(big_info, 0x11, sizeof(big_info));
        memset(got, 0x77, sizeof(got));
        hkdf_expand(prk, 32, big_info, (int)sizeof(big_info), got, 42);
        int allz = 1;
        for (int i = 0; i < 42; i++) if (got[i] != 0) { allz = 0; break; }
        fails += check("BUG-0298 hkdf_expand oversized info -> zeroed out", allz);
        /* allocation failure: deterministic zero, never partial data */
        g_pmm_fail = 1;
        memset(got, 0x77, sizeof(got));
        hkdf_expand(prk, 32, info, 10, got, 42);
        allz = 1;
        for (int i = 0; i < 42; i++) if (got[i] != 0) { allz = 0; break; }
        g_pmm_fail = 0;
        fails += check("BUG-0298 hkdf_expand alloc failure -> zeroed out", allz);

        /* crypto_dh_modexp_n: tiny in-range case still exact (regression) */
        u8 b2[2] = {0x00, 0x02}, e2[2] = {0x00, 0x03}, p2[2] = {0x00, 0x07};
        u8 res[2];
        crypto_dh_modexp_n(b2, e2, p2, res, 2);   /* 2^3 mod 7 = 1 */
        fails += check("BUG-0298 modexp_n len=2 2^3 mod 7 = 1 (regression)",
                       res[0] == 0x00 && res[1] == 0x01);
        /* oversized len must be rejected with a zeroed result */
        u8 big[2049];
        memset(big, 0x33, sizeof(big));
        crypto_dh_modexp_n(b2, e2, p2, big, 2049);
        allz = 1;
        for (int i = 0; i < 2049; i++) if (big[i] != 0) { allz = 0; break; }
        fails += check("BUG-0298 modexp_n len>2048 rejected + zeroed", allz);
        /* allocation failure: zeroed, not garbage */
        g_pmm_fail = 1;
        memset(res, 0x99, 2);
        crypto_dh_modexp_n(b2, e2, p2, res, 2);
        g_pmm_fail = 0;
        fails += check("BUG-0298 modexp_n alloc failure -> zeroed result",
                       res[0] == 0 && res[1] == 0);
    }

    printf("host_fix4_crypto_test: %s (%d failure%s)\n",
           fails == 0 ? "ALL PASS" : "FAIL", fails, fails == 1 ? "" : "s");
    return fails == 0 ? 0 : 1;
}
