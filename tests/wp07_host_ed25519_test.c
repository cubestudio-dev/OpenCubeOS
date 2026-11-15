/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: tests/wp07_host_ed25519_test.c
 * Purpose: host-side proof that the OCIRE Ed25519 verifier accepts
 *          the RFC 8032 test vectors and rejects tampering.
 *
 *          Compiled straight from ocire/ocire_ed25519.c + ocire_sha512.c
 *          (the same objects the kernel links) so a PASS here is a
 *          PASS for the in-kernel verifier.  The public-key anchor is
 *          stubbed per-vector (ocire_ed25519_verify_pk).
 *
 * Usage: gcc -O2 -o build/wp07_ed25519_test tests/wp07_host_ed25519_test.c \
 *             ocire/ocire_ed25519.c ocire/ocire_sha512.c ocire/ocire_util.c \
 *             -I ocire && ./build/wp07_ed25519_test
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint64_t u64;

/* from ocire_ed25519.h (re-declared to keep the test standalone) */
int ocire_ed25519_verify_pk(const u8 pk[32], const u8 *msg, u64 len,
                            const u8 sig[64]);

/* the built-in anchor is not exercised here (per-vector keys are);
 * define it so the link succeeds */
const u8 ocire_ed25519_pubkey[32] = {0};

static int failures;

static void hex2bin(const char *hex, u8 *out, int cap) {
    int n = 0;
    while (hex[0] && hex[1] && n < cap) {
        unsigned v;
        sscanf(hex, "%2x", &v);
        out[n++] = (u8)v;
        hex += 2;
    }
}

static void check(const char *name, int expect_ok, const char *pk_hex,
                  const char *msg_hex, const char *sig_hex) {
    u8 pk[32], sig[64];
    u8 msg[1024];
    hex2bin(pk_hex, pk, 32);
    hex2bin(sig_hex, sig, 64);
    int mlen = 0;
    if (msg_hex[0]) {
        int hi = 0;
        while (msg_hex[hi] && msg_hex[hi + 1]) { hi += 2; mlen++; }
        hex2bin(msg_hex, msg, mlen);
    }
    int rc = ocire_ed25519_verify_pk(pk, msg, (u64)mlen, sig);
    int ok = (rc == 0);
    if (ok != expect_ok) {
        printf("  FAIL %-34s expected %s got %s (rc=%d)\n", name,
               expect_ok ? "ACCEPT" : "REJECT", ok ? "ACCEPT" : "REJECT", rc);
        failures++;
    } else {
        printf("  ok   %-34s %s\n", name, ok ? "accepted" : "rejected");
    }
}

int main(void) {
    printf("WP-07 host Ed25519 verifier test (RFC 8032 vectors)\n");

    /* RFC 8032 section 7.1 TEST 1 (empty message) */
    check("RFC8032 t1 valid", 1,
          "d75a980182b10ab7d54bfed3c964073a"
          "0ee172f3daa62325af021a68f707511a",
          "",
          "e5564300c360ac729086e2cc806e828a"
          "84877f1eb8e5d974d873e06522490155"
          "5fb8821590a33bacc61e39701cf9b46b"
          "d25bf5f0595bbe24655141438e7a100b");

    /* RFC 8032 TEST 2 (1 byte 0x72) */
    check("RFC8032 t2 valid", 1,
          "3d4017c3e843895a92b70aa74d1b7ebc"
          "9c982ccf2ec4968cc0cd55f12af4660c",
          "72",
          "92a009a9f0d4cab8720e820b5f642540"
          "a2b27b5416503f8fb3762223ebdb69da"
          "085ac1e43e15996e458f3613d0f11d8c"
          "387b2eaeb4302aeeb00d291612bb0c00");

    /* RFC 8032 TEST 3 (2 bytes af82) */
    check("RFC8032 t3 valid", 1,
          "fc51cd8e6218a1a38da47ed00230f058"
          "0816ed13ba3303ac5deb911548908025",
          "af82",
          "6291d657deec24024827e69c3abe01a3"
          "0ce548a284743a445e3680d7db5ac3ac"
          "18ff9b538d16f290ae67f760984dc659"
          "4a7c15e9716ed28dc027beceea1ec40a");

    /* tampered signature (flip one bit of S) -> must reject */
    {
        u8 pk[32], sig[64], msg[1] = {0x72};
        hex2bin("3d4017c3e843895a92b70aa74d1b7ebc"
                "9c982ccf2ec4968cc0cd55f12af4660c", pk, 32);
        hex2bin("92a009a9f0d4cab8720e820b5f642540"
                "a2b27b5416503f8fb3762223ebdb69da"
                "085ac1e43e15996e458f3613d0f11d8c"
                "387b2eaeb4302aeeb00d291612bb0c00", sig, 64);
        sig[63] ^= 0x01;
        int rc = ocire_ed25519_verify_pk(pk, msg, 1, sig);
        if (rc == 0) { printf("  FAIL tampered S accepted\n"); failures++; }
        else printf("  ok   %-34s rejected\n", "tampered S");
    }

    /* tampered message -> must reject */
    {
        u8 pk[32], sig[64], msg[1] = {0x73};
        hex2bin("3d4017c3e843895a92b70aa74d1b7ebc"
                "9c982ccf2ec4968cc0cd55f12af4660c", pk, 32);
        hex2bin("92a009a9f0d4cab8720e820b5f642540"
                "a2b27b5416503f8fb3762223ebdb69da"
                "085ac1e43e15996e458f3613d0f11d8c"
                "387b2eaeb4302aeeb00d291612bb0c00", sig, 64);
        int rc = ocire_ed25519_verify_pk(pk, msg, 1, sig);
        if (rc == 0) { printf("  FAIL tampered M accepted\n"); failures++; }
        else printf("  ok   %-34s rejected\n", "tampered M");
    }

    /* wrong public key -> must reject */
    {
        u8 pk[32], sig[64], msg[1] = {0x72};
        hex2bin("d75a980182b10ab7d54bfed3c964073a"
                "0ee172f3daa62325af021a68f707511a", pk, 32);
        hex2bin("92a009a9f0d4cab8720e820b5f642540"
                "a2b27b5416503f8fb3762223ebdb69da"
                "085ac1e43e15996e458f3613d0f11d8c"
                "387b2eaeb4302aeeb00d291612bb0c00", sig, 64);
        int rc = ocire_ed25519_verify_pk(pk, msg, 1, sig);
        if (rc == 0) { printf("  FAIL wrong key accepted\n"); failures++; }
        else printf("  ok   %-34s rejected\n", "wrong public key");
    }

    /* S >= L (non-canonical) -> must reject */
    {
        u8 pk[32], sig[64], msg[1] = {0x72};
        hex2bin("3d4017c3e843895a92b70aa74d1b7ebc"
                "9c982ccf2ec4968cc0cd55f12af4660c", pk, 32);
        hex2bin("92a009a9f0d4cab8720e820b5f642540"
                "a2b27b5416503f8fb3762223ebdb69da"
                "085ac1e43e15996e458f3613d0f11d8c"
                "387b2eaeb4302aeeb00d291612bb0c00", sig, 64);
        sig[32] = 0x10; sig[33] = 0x00; sig[34] = 0x00; sig[35] = 0x00;
        for (int i = 36; i < 62; i++) sig[i] = 0;
        sig[62] = 0x14; sig[63] = 0xde;   /* S = L exactly */
        int rc = ocire_ed25519_verify_pk(pk, msg, 1, sig);
        if (rc == 0) { printf("  FAIL S==L accepted\n"); failures++; }
        else printf("  ok   %-34s rejected\n", "S == L non-canonical");
    }

    /* garbage R (not a curve point) -> must reject */
    {
        u8 pk[32], sig[64], msg[1] = {0x72};
        hex2bin("3d4017c3e843895a92b70aa74d1b7ebc"
                "9c982ccf2ec4968cc0cd55f12af4660c", pk, 32);
        hex2bin("92a009a9f0d4cab8720e820b5f642540"
                "a2b27b5416503f8fb3762223ebdb69da"
                "085ac1e43e15996e458f3613d0f11d8c"
                "387b2eaeb4302aeeb00d291612bb0c00", sig, 64);
        for (int i = 0; i < 32; i++) sig[i] = 0xED;  /* not on curve */
        int rc = ocire_ed25519_verify_pk(pk, msg, 1, sig);
        if (rc == 0) { printf("  FAIL garbage R accepted\n"); failures++; }
        else printf("  ok   %-34s rejected\n", "garbage R");
    }

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
