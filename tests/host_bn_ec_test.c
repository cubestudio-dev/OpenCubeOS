/* Host-side unit test for bn.c + ec_nist.c (NOT part of the kernel build).
 * Verifies against NIST P-256 ECDSA known-answer vector and ECDH
 * self-consistency + known public key. Built only for development testing:
 *   gcc -O2 -Ikernel tests/host_bn_ec_test.c kernel/bn.c \
 *       kernel/ec_nist.c -o /tmp/bn_ec_test
 */
#include <stdio.h>
#include "string.h"  /* kernel string.h — use oc_* fns in host tests too */
/* libc string.h is included first on purpose; kernel headers only add oc_*
 * functions and must not shadow libc declarations in this host test. */
#include "bn.h"
#include "ec_nist.h"

/* crypto_random stub for host test (deterministic) */
void crypto_random(u8 *buf, int len) {
    for (int i = 0; i < len; i++) buf[i] = (u8)(0xA5 + i);
}

static int hex2bin(const char *h, u8 *out, int n) {
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (u8)v;
    }
    return 0;
}

static void dump(const char *name, const u8 *b, int n) {
    printf("%s: ", name);
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

/* NIST CAVP P-256 ECDSAPrvKey186-4 / SigGen186-4 vector ("sample") */
static const char *NIST_D =
    "C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721";
static const char *NIST_QX =
    "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6";
static const char *NIST_QY =
    "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
static const char *NIST_R =
    "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716";
static const char *NIST_S =
    "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";
/* SHA-256("sample") */
static const char *NIST_H =
    "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF";

int main(void) {
    int fails = 0;
    u8 d[32], qx[32], qy[32], r[32], s[32], h[32];
    hex2bin(NIST_D, d, 32);
    hex2bin(NIST_QX, qx, 32);
    hex2bin(NIST_QY, qy, 32);
    hex2bin(NIST_R, r, 32);
    hex2bin(NIST_S, s, 32);
    hex2bin(NIST_H, h, 32);

    /* --- Test 1: bn_mod_exp basic --- */
    u8 out[32];
    /* 3^5 mod 7 = 5 */
    u8 b3 = 3, e5 = 5, m7 = 7;
    bn_mod_exp(out, 1, &b3, 1, &e5, 1, &m7, 1);
    printf("bn_mod_exp 3^5 mod 7 = %d (expect 5): %s\n", out[0],
           out[0] == 5 ? "PASS" : "FAIL");
    if (out[0] != 5) fails++;

    /* --- Test 2: pub from priv matches NIST Q --- */
    u8 pub[65];
    if (ec_pub_from_priv(EC_P256, d, 32, pub, 65) != 0) {
        printf("ec_pub_from_priv: FAIL (error)\n");
        fails++;
    } else {
        int ok = oc_memcmp(pub + 1, qx, 32) == 0 && oc_memcmp(pub + 33, qy, 32) == 0;
        printf("ec_pub_from_priv == NIST Q: %s\n", ok ? "PASS" : "FAIL");
        if (!ok) { dump("got x", pub + 1, 32); dump("got y", pub + 33, 32); fails++; }
    }

    /* --- Test 3: ECDSA verify (valid sig) --- */
    int v = ecdsa_verify(EC_P256, pub, 65, h, 32, r, 32, s, 32);
    printf("ecdsa_verify valid: %s (v=%d)\n", v == 1 ? "PASS" : "FAIL", v);
    if (v != 1) fails++;

    /* --- Test 4: ECDSA verify (corrupted s -> reject) --- */
    u8 s2[32];
    oc_memcpy(s2, s, 32);
    s2[31] ^= 1;
    v = ecdsa_verify(EC_P256, pub, 65, h, 32, r, 32, s2, 32);
    printf("ecdsa_verify corrupted: %s (v=%d)\n", v == 0 ? "PASS" : "FAIL", v);
    if (v != 0) fails++;

    /* --- Test 5: ECDH self-consistency P-256 --- */
    u8 da[32], db[32], pa[65], pb[65], sa[32], sb[32];
    if (ec_keygen(EC_P256, da, 32, pa, 65) != 0 ||
        ec_keygen(EC_P256, db, 32, pb, 65) != 0) {
        printf("ec_keygen: FAIL\n");
        fails++;
    } else if (ec_ecdh(EC_P256, da, 32, pb, 65, sa, 32) != 0 ||
               ec_ecdh(EC_P256, db, 32, pa, 65, sb, 32) != 0) {
        printf("ec_ecdh: FAIL (error)\n");
        fails++;
    } else {
        int ok = oc_memcmp(sa, sb, 32) == 0;
        printf("ec_ecdh self-consistency: %s\n", ok ? "PASS" : "FAIL");
        if (!ok) { dump("sa", sa, 32); dump("sb", sb, 32); fails++; }
    }

    /* --- Test 6: ECDH with invalid point must fail --- */
    u8 bad[65];
    oc_memcpy(bad, pb, 65);
    bad[64] ^= 0x01;
    v = ec_ecdh(EC_P256, da, 32, bad, 65, sa, 32);
    printf("ec_ecdh invalid point: %s (v=%d)\n", v == -1 ? "PASS" : "FAIL", v);
    if (v != -1) fails++;

    /* --- Test 7: P-384 pub-from-priv runs and verifies on curve --- */
    u8 d384[48], p384[97];
    crypto_random(d384, 48);
    d384[0] &= 0x3f;
    if (ec_pub_from_priv(EC_P384, d384, 48, p384, 97) != 0 ||
        ec_pub_valid(EC_P384, p384, 97) != 1) {
        printf("ec_p384 scalar mult + on-curve: FAIL\n");
        fails++;
    } else {
        printf("ec_p384 scalar mult + on-curve: PASS\n");
    }

    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
