/* Host-side unit test for aead.c + sha512.c (NOT part of the kernel build).
 * Uses official vectors: RFC 8439 2.5.2 (Poly1305), RFC 8439 2.8.2
 * (ChaCha20-Poly1305 AEAD, also validates the ChaCha20 stream), NIST GCM
 * spec Test Case 3/4, FIPS 180-4 ("abc" digests).
 * Build: gcc -O2 -Ikernel tests/host_aead_test.c kernel/aead.c \
 *        kernel/sha512.c kernel/string.c -o /tmp/aead_test
 */
#include <stdio.h>
#include "types.h"
#include "aead.h"
#include "sha512.h"

void *oc_memset(void *d, int c, unsigned long n);
void *oc_memcpy(void *d, const void *s, unsigned long n);
int   oc_memcmp(const void *a, const void *b, unsigned long n);

static int hex2bin(const char *h, u8 *out, int n) {
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (u8)v;
    }
    return 0;
}
static int fails = 0;
static void check(const char *name, int ok) {
    printf("%-42s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) fails++;
}

/* RFC 8439 2.8.2 (also the ChaCha20 stream + Poly1305 validation) */
static const char *AE_KEY =
    "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
static const char *AE_NONCE = "070000004041424344454647";
static const char *AE_AAD   = "50515253c0c1c2c3c4c5c6c7";
static const char *AE_PT    =
    "4c616469657320616e642047656e746c656d656e206f662074686520636c6173"
    "73206f66202739393a204966204920636f756c64206f6666657220796f75206f"
    "6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73"
    "637265656e20776f756c642062652069742e";
static const char *AE_CT    =
    "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
    "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
    "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
    "3ff4def08e4b7a9de576d26586cec64b6116";
static const char *AE_TAG   = "1ae10b594f09e26a7e902ecbd0600691";

/* RFC 8439 2.5.2 */
static const char *PL_KEY =
    "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b";
static const char *PL_TAG = "a8061dc1305136c6c22b8baf0c0127a9";

/* NIST GCM spec Test Case 3 (AES-128, no AAD) */
static const char *G3_KEY = "feffe9928665731c6d6a8f9467308308";
static const char *G3_IV  = "cafebabefacedbaddecaf888";
static const char *G3_PT  =
    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
    "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255";
static const char *G3_CT  =
    "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
    "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985";
static const char *G3_TAG = "4d5c2af327cd64a62cf35abd2ba6fab4";

/* NIST GCM spec Test Case 4 (AES-128 with AAD) */
static const char *G4_AAD = "feedfacedeadbeeffeedfacedeadbeefabaddad2";
static const char *G4_CT  =
    "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
    "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091";
static const char *G4_TAG = "5bc94fbc3221a5db94fae95ae7121a47";

/* SHA-512/384("abc") */
static const char *S512_ABC =
    "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
    "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f";
static const char *S384_ABC =
    "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
    "8086072ba1e7cc2358baeca134c825a7";

#define MAXD 128
static int hexlen(const char *h) { int n = 0; while (h[n]) n++; return n / 2; }

int main(void) {
    u8 buf[MAXD + 16], buf2[MAXD + 16], tag[16], key[32], nonce[12];

    /* --- Poly1305 RFC 8439 2.5.2 --- */
    {
        u8 k[32], want[16];
        hex2bin(PL_KEY, k, 32);
        hex2bin(PL_TAG, want, 16);
        const char *msg = "Cryptographic Forum Research Group";
        poly1305_mac(k, (const u8 *)msg, 34, tag);
        check("poly1305_mac RFC8439 2.5.2", oc_memcmp(tag, want, 16) == 0);
    }

    /* --- ChaCha20 stream via AEAD vector plaintext XOR --- */
    {
        u8 k[32], n[12], pt[MAXD], ct[MAXD], want[MAXD];
        hex2bin(AE_KEY, k, 32);
        hex2bin(AE_NONCE, n, 12);
        int pl = hexlen(AE_PT);
        hex2bin(AE_PT, pt, pl);
        hex2bin(AE_CT, want, pl);
        chacha20_xor(k, n, 1, pt, pl, ct);
        check("chacha20_xor stream RFC8439 2.8.2", oc_memcmp(ct, want, pl) == 0);
    }

    /* --- ChaCha20-Poly1305 AEAD seal/open --- */
    {
        u8 k[32], n[12], a[12], pt[MAXD], ct[MAXD], want_tag[16], want_ct[MAXD];
        hex2bin(AE_KEY, k, 32);
        hex2bin(AE_NONCE, n, 12);
        hex2bin(AE_AAD, a, 12);
        int pl = hexlen(AE_PT);
        hex2bin(AE_PT, pt, pl);
        hex2bin(AE_CT, want_ct, pl);
        hex2bin(AE_TAG, want_tag, 16);
        chacha20poly1305_seal(k, n, a, 12, pt, pl, ct, tag);
        check("chacha20poly1305 ct RFC8439 2.8.2", oc_memcmp(ct, want_ct, pl) == 0);
        check("chacha20poly1305 tag RFC8439 2.8.2", oc_memcmp(tag, want_tag, 16) == 0);
        int rc = chacha20poly1305_open(k, n, a, 12, ct, pl, buf, tag);
        check("chacha20poly1305 open roundtrip", rc == 0 && oc_memcmp(buf, pt, pl) == 0);
        tag[0] ^= 1;
        rc = chacha20poly1305_open(k, n, a, 12, ct, pl, buf, tag);
        check("chacha20poly1305 bad tag rejected", rc == -1);
    }

    /* --- AES-128-GCM NIST TC3 (no AAD) + TC4 (with AAD) --- */
    {
        u8 k[16], n[12], pt[MAXD], ct[MAXD], want_ct[MAXD], want_tag[16], a[20];
        hex2bin(G3_KEY, k, 16);
        hex2bin(G3_IV, n, 12);
        int pl = hexlen(G3_PT);
        hex2bin(G3_PT, pt, pl);
        hex2bin(G3_CT, want_ct, pl);
        hex2bin(G3_TAG, want_tag, 16);
        aes128_gcm_seal(k, n, NULL, 0, pt, pl, ct, tag);
        check("aes128-gcm ct NIST TC3", oc_memcmp(ct, want_ct, pl) == 0);
        check("aes128-gcm tag NIST TC3", oc_memcmp(tag, want_tag, 16) == 0);
        int rc = aes128_gcm_open(k, n, NULL, 0, ct, pl, buf, tag);
        check("aes128-gcm open roundtrip", rc == 0 && oc_memcmp(buf, pt, pl) == 0);

        int al = hexlen(G4_AAD);
        hex2bin(G4_AAD, a, al);
        int cl = hexlen(G4_CT);
        hex2bin(G4_CT, want_ct, cl);
        hex2bin(G4_TAG, want_tag, 16);
        /* TC4 plaintext = first 60 bytes of TC3's plaintext */
        aes128_gcm_seal(k, n, a, al, pt, cl, ct, tag);
        check("aes128-gcm ct NIST TC4", oc_memcmp(ct, want_ct, cl) == 0);
        check("aes128-gcm tag NIST TC4", oc_memcmp(tag, want_tag, 16) == 0);
        rc = aes128_gcm_open(k, n, a, al, ct, cl, buf, tag);
        check("aes128-gcm TC4 open", rc == 0 && oc_memcmp(buf, pt, cl) == 0);
    }

    /* --- AES-256-GCM with AAD roundtrip --- */
    {
        u8 k[32], n[12], a2[20], pt[MAXD], ct[MAXD];
        oc_memset(k, 0, 32);
        hex2bin(G3_KEY, k, 16);
        hex2bin(G3_IV, n, 12);
        int al = hexlen(G4_AAD);
        hex2bin(G4_AAD, a2, al);
        oc_memcpy(a2 + al, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
        al += 8;
        int pl = hexlen(G3_PT);
        hex2bin(G3_PT, pt, pl);
        aes256_gcm_seal(k, n, a2, al, pt, pl, ct, tag);
        int rc = aes256_gcm_open(k, n, a2, al, ct, pl, buf, tag);
        check("aes256-gcm aad roundtrip", rc == 0 && oc_memcmp(buf, pt, pl) == 0);
    }

    /* --- SHA-512/384 --- */
    {
        u8 want[64], got[64];
        hex2bin(S512_ABC, want, 64);
        sha512((const u8 *)"abc", 3, got);
        check("sha512(abc) FIPS180-4", oc_memcmp(got, want, 64) == 0);
        hex2bin(S384_ABC, want, 48);
        sha384((const u8 *)"abc", 3, got);
        check("sha384(abc) FIPS180-4", oc_memcmp(got, want, 48) == 0);
        sha512_ctx_t c;
        sha512_init(&c);
        sha512_update(&c, (const u8 *)"a", 1);
        sha512_update(&c, (const u8 *)"bc", 2);
        sha512_final(&c, got);
        sha512((const u8 *)"abc", 3, buf);
        check("sha512 incremental", oc_memcmp(got, buf, 64) == 0);
        u8 big[300];
        for (int i = 0; i < 300; i++) big[i] = (u8)(i * 7 + 3);
        sha512_ctx_t c2;
        sha512_init(&c2);
        sha512_update(&c2, big, 300);
        sha512_final(&c2, got);
        sha512(big, 300, buf);
        check("sha512 300B incremental", oc_memcmp(got, buf, 64) == 0);
    }

    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
