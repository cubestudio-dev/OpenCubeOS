/* Host-side unit test for aead.c + sha512.c (NOT part of the kernel build).
 * Uses official vectors: RFC 8439 2.5.2 (Poly1305), RFC 8439 2.8.2
 * (ChaCha20-Poly1305 AEAD, also validates the ChaCha20 stream), NIST GCM
 * spec Test Case 3/4, FIPS 180-4 ("abc" digests).
 * Build: gcc -O2 -Ikernel tests/host_aead_test.c kernel/aead.c \
 *        kernel/sha512.c kernel/string.c -o /tmp/aead_test
 */
#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "crypto_aead.h"
#include "crypto_sha512.h"

void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
int   memcmp(const void *a, const void *b, unsigned long n);

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

/* RFC 4231 (HMAC-SHA-2 KATs) - BUG-0292 (A4-05) FIX regression:
 * crypto_hmac_sha384 used to be HMAC-SHA-512 truncation; these vectors pin
 * the real SHA-384 IV + SHA-384 key-hash behaviour. TC6 exercises the
 * key-longer-than-block (128 B) path (SHA-384 key hash, 48-byte padded). */
static const char *H384_T1 =
    "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59c"
    "faea9ea9076ede7f4af152e8b2fa9cb6";
static const char *H384_T2 =
    "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e"
    "8e2240ca5e69e2c78b3239ecfab21649";
static const char *H384_T3 =
    "88062608d3e6ad8a0aa2ace014c8a86f0aa635d947ac9febe83ef4e55966144b"
    "2a5ab39dc13814b94e3ab6e101a34f27";
static const char *H384_T4 =
    "3e8a69b7783c25851933ab6290af6ca77a9981480850009cc5577c6e1f573b4e"
    "6801dd23c4a7d679ccf8a386c674cffb";
static const char *H384_T6 =
    "4ece084485813e9088d2c63a041bc5b44f9ef1012a2b588f3cd11f05033ac4c6"
    "0c2ef6ab4030fe8296248df163f44952";
static const char *H512_T1 =
    "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
    "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854";
static const char *H512_T2 =
    "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
    "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737";

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
        crypto_poly1305_mac(k, (const u8 *)msg, 34, tag);
        check("poly1305_mac RFC8439 2.5.2", memcmp(tag, want, 16) == 0);
    }

    /* --- ChaCha20 stream via AEAD vector plaintext XOR --- */
    {
        u8 k[32], n[12], pt[MAXD], ct[MAXD], want[MAXD];
        hex2bin(AE_KEY, k, 32);
        hex2bin(AE_NONCE, n, 12);
        int pl = hexlen(AE_PT);
        hex2bin(AE_PT, pt, pl);
        hex2bin(AE_CT, want, pl);
        crypto_chacha20_xor(k, n, 1, pt, pl, ct);
        check("chacha20_xor stream RFC8439 2.8.2", memcmp(ct, want, pl) == 0);
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
        check("chacha20poly1305 ct RFC8439 2.8.2", memcmp(ct, want_ct, pl) == 0);
        check("chacha20poly1305 tag RFC8439 2.8.2", memcmp(tag, want_tag, 16) == 0);
        int rc = chacha20poly1305_open(k, n, a, 12, ct, pl, buf, tag);
        check("chacha20poly1305 open roundtrip", rc == 0 && memcmp(buf, pt, pl) == 0);
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
        crypto_aes128_gcm_seal(k, n, NULL, 0, pt, pl, ct, tag);
        check("aes128-gcm ct NIST TC3", memcmp(ct, want_ct, pl) == 0);
        check("aes128-gcm tag NIST TC3", memcmp(tag, want_tag, 16) == 0);
        int rc = crypto_aes128_gcm_open(k, n, NULL, 0, ct, pl, buf, tag);
        check("aes128-gcm open roundtrip", rc == 0 && memcmp(buf, pt, pl) == 0);

        int al = hexlen(G4_AAD);
        hex2bin(G4_AAD, a, al);
        int cl = hexlen(G4_CT);
        hex2bin(G4_CT, want_ct, cl);
        hex2bin(G4_TAG, want_tag, 16);
        /* TC4 plaintext = first 60 bytes of TC3's plaintext */
        crypto_aes128_gcm_seal(k, n, a, al, pt, cl, ct, tag);
        check("aes128-gcm ct NIST TC4", memcmp(ct, want_ct, cl) == 0);
        check("aes128-gcm tag NIST TC4", memcmp(tag, want_tag, 16) == 0);
        rc = crypto_aes128_gcm_open(k, n, a, al, ct, cl, buf, tag);
        check("aes128-gcm TC4 open", rc == 0 && memcmp(buf, pt, cl) == 0);
    }

    /* --- AES-256-GCM with AAD roundtrip --- */
    {
        u8 k[32], n[12], a2[20], pt[MAXD], ct[MAXD];
        memset(k, 0, 32);
        hex2bin(G3_KEY, k, 16);
        hex2bin(G3_IV, n, 12);
        int al = hexlen(G4_AAD);
        hex2bin(G4_AAD, a2, al);
        memcpy(a2 + al, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
        al += 8;
        int pl = hexlen(G3_PT);
        hex2bin(G3_PT, pt, pl);
        crypto_aes256_gcm_seal(k, n, a2, al, pt, pl, ct, tag);
        int rc = crypto_aes256_gcm_open(k, n, a2, al, ct, pl, buf, tag);
        check("aes256-gcm aad roundtrip", rc == 0 && memcmp(buf, pt, pl) == 0);
    }

    /* --- SHA-512/384 --- */
    {
        u8 want[64], got[64];
        hex2bin(S512_ABC, want, 64);
        sha512((const u8 *)"abc", 3, got);
        check("sha512(abc) FIPS180-4", memcmp(got, want, 64) == 0);
        hex2bin(S384_ABC, want, 48);
        sha384((const u8 *)"abc", 3, got);
        check("sha384(abc) FIPS180-4", memcmp(got, want, 48) == 0);
        crypto_sha512_ctx_t c;
        crypto_sha512_init(&c);
        crypto_sha512_update(&c, (const u8 *)"a", 1);
        crypto_sha512_update(&c, (const u8 *)"bc", 2);
        crypto_sha512_final(&c, got);
        sha512((const u8 *)"abc", 3, buf);
        check("sha512 incremental", memcmp(got, buf, 64) == 0);
        u8 big[300];
        for (int i = 0; i < 300; i++) big[i] = (u8)(i * 7 + 3);
        crypto_sha512_ctx_t c2;
        crypto_sha512_init(&c2);
        crypto_sha512_update(&c2, big, 300);
        crypto_sha512_final(&c2, got);
        sha512(big, 300, buf);
        check("sha512 300B incremental", memcmp(got, buf, 64) == 0);
    }

    /* --- RFC 4231 HMAC-SHA-384 / HMAC-SHA-512 KATs (BUG-0292 FIX) --- */
    {
        u8 want[64], got[64];
        u8 k1[20], k3[20], k4[25], k6[131], d3[50], d4[50];
        memset(k1, 0x0b, 20);
        memset(k3, 0xaa, 20);
        memset(k4, 0, 25); for (int i = 0; i < 25; i++) k4[i] = (u8)(i + 1);
        memset(k6, 0xaa, 131);
        memset(d3, 0xdd, 50);
        memset(d4, 0xcd, 50);
        const char *t2d = "what do ya want for nothing?";
        const char *t6d = "Test Using Larger Than Block-Size Key - Hash Key First";

        crypto_hmac_sha384(k1, 20, (const u8 *)"Hi There", 8, got);
        hex2bin(H384_T1, want, 48);
        check("hmac-sha384 RFC4231 TC1", memcmp(got, want, 48) == 0);
        crypto_hmac_sha384((const u8 *)"Jefe", 4, (const u8 *)t2d, 28, got);
        hex2bin(H384_T2, want, 48);
        check("hmac-sha384 RFC4231 TC2", memcmp(got, want, 48) == 0);
        crypto_hmac_sha384(k3, 20, d3, 50, got);
        hex2bin(H384_T3, want, 48);
        check("hmac-sha384 RFC4231 TC3", memcmp(got, want, 48) == 0);
        crypto_hmac_sha384(k4, 25, d4, 50, got);
        hex2bin(H384_T4, want, 48);
        check("hmac-sha384 RFC4231 TC4", memcmp(got, want, 48) == 0);
        crypto_hmac_sha384(k6, 131, (const u8 *)t6d, 54, got);
        hex2bin(H384_T6, want, 48);
        check("hmac-sha384 RFC4231 TC6 (key>block)", memcmp(got, want, 48) == 0);

        crypto_hmac_sha512(k1, 20, (const u8 *)"Hi There", 8, got);
        hex2bin(H512_T1, want, 64);
        check("hmac-sha512 RFC4231 TC1", memcmp(got, want, 64) == 0);
        crypto_hmac_sha512((const u8 *)"Jefe", 4, (const u8 *)t2d, 28, got);
        hex2bin(H512_T2, want, 64);
        check("hmac-sha512 RFC4231 TC2", memcmp(got, want, 64) == 0);
    }

    /* --- BUG-0293 FIX regression: fail-closed length validation, no shared
     * staging buffer. Validation happens before any buffer is read, so the
     * rejected calls below are safe to make with small/NULL buffers. --- */
    {
        u8 k[32], n[12], pt[16], ct[64], out[64], tag[16];
        memset(k, 1, 32); memset(n, 2, 12); memset(pt, 3, 16);
        check("aead seal negative pt_len rejected",
              chacha20poly1305_seal(k, n, NULL, 0, pt, -1, ct, tag) == -1);
        check("aead seal negative aad_len rejected",
              chacha20poly1305_seal(k, n, pt, -1, pt, 16, ct, tag) == -1);
        check("aead seal over-cap aad rejected",
              chacha20poly1305_seal(k, n, pt, (1 << 30) + 1, pt, 16, ct, tag) == -1);
        check("aead open negative ct_len rejected",
              chacha20poly1305_open(k, n, NULL, 0, pt, -1, out, tag) == -1);
        check("aead open over-cap ct rejected",
              chacha20poly1305_open(k, n, NULL, 0, pt, (1 << 30) + 1, out, tag) == -1);
        /* streaming tail-register coverage with non-multiple-of-16 lengths */
        u8 odd[37];
        for (int i = 0; i < 37; i++) odd[i] = (u8)(i * 5 + 1);
        int rc = chacha20poly1305_seal(k, n, odd, 37, odd, 37, ct, tag);
        check("aead 37B aad/pt seal", rc == 0);
        rc = chacha20poly1305_open(k, n, odd, 37, ct, 37, out, tag);
        check("aead 37B aad/pt open roundtrip", rc == 0 && memcmp(out, odd, 37) == 0);
        /* BUG-0293 before-POC boundary: aad16 + pt20500 used to exceed the old
         * 20480 B shared staging buffer and produced an all-0xEE tag with seal
         * returning 0. Streaming mac_data has no staging cap, so the exact
         * same input must now seal AND open correctly (heap buffers, so the
         * huge memcpy into the old static buffer can never happen again). */
        {
            size_t pt_len = 20500;      /* old OC_MAC_BUF_SIZE boundary */
            size_t aad2_len = 20513;    /* large aad, non-multiple-of-16 */
            u8 *aad2 = malloc(aad2_len);
            u8 *buf = malloc(2 * pt_len);          /* ct | pt */
            if (!aad2 || !buf) {
                check("aead oversize buffers alloc", 0);
            } else {
                u8 *bct = buf, *bpt = buf + pt_len;
                int rc, same;
                for (size_t i = 0; i < aad2_len; i++) aad2[i] = (u8)(i * 7 + 3);
                for (size_t i = 0; i < 2 * pt_len; i++) buf[i] = (u8)(i * 11 + 5);
                rc = chacha20poly1305_seal(k, n, aad2, 16, bpt, (int)pt_len, bct, tag);
                check("aead 20500B pt (old 20480B boundary) seal", rc == 0);
                rc = chacha20poly1305_open(k, n, aad2, 16, bct, (int)pt_len, bpt, tag);
                same = 1;
                for (size_t i = 0; i < pt_len; i++)
                    if (bpt[i] != (u8)((pt_len + i) * 11 + 5)) same = 0;
                check("aead 20500B pt (old 20480B boundary) open+content",
                      rc == 0 && same);
                /* large aad across many streaming blocks + odd tail */
                rc = chacha20poly1305_seal(k, n, aad2, (int)aad2_len, bpt, (int)pt_len, bct, tag);
                check("aead 20513B aad seal", rc == 0);
                rc = chacha20poly1305_open(k, n, aad2, (int)aad2_len, bct, (int)pt_len, bpt, tag);
                same = 1;
                for (size_t i = 0; i < pt_len; i++)
                    if (bpt[i] != (u8)((pt_len + i) * 11 + 5)) same = 0;
                check("aead 20513B aad open+content", rc == 0 && same);
                free(aad2);
                free(buf);
            }
        }
    }

    printf(fails ? "== %d FAILURES ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
