/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_sha256.c
 * Purpose: SHA-256 (see ocire_sha256.h).  The round constants and the
 * message schedule follow FIPS 180-4 section 6.2; verified against
 * the NIST example vectors ("abc" ->
 * ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad).
 */

#include "ocire_sha256.h"
#include "ocire_util.h"

static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
    0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
    0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
    0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
    0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
    0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(ocire_sha256_ctx *c, const u8 *p) {
    u32 w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((u32)p[i*4] << 24) | ((u32)p[i*4+1] << 16) |
               ((u32)p[i*4+2] << 8) | (u32)p[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROTR(w[i-15],7) ^ ROTR(w[i-15],18) ^ (w[i-15] >> 3);
        u32 s1 = ROTR(w[i-2],17) ^ ROTR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    u32 e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = ROTR(e,6) ^ ROTR(e,11) ^ ROTR(e,25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + S1 + ch + K[i] + w[i];
        u32 S0 = ROTR(a,2) ^ ROTR(a,13) ^ ROTR(a,22);
        u32 mj = (a & b) ^ (a & cc) ^ (b & cc);
        u32 t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void ocire_sha256_init(ocire_sha256_ctx *c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372;
    c->h[3]=0xa54ff53a; c->h[4]=0x510e527f; c->h[5]=0x9b05688c;
    c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->len = 0;
    c->buflen = 0;
}

void ocire_sha256_update(ocire_sha256_ctx *c, const void *data, u64 len) {
    const u8 *p = (const u8 *)data;
    c->len += len;
    while (len > 0) {
        u32 take = 64 - c->buflen;
        if (take > len) take = (u32)len;
        oc_memcpy(c->buf + c->buflen, p, take);
        c->buflen += take;
        p += take;
        len -= take;
        if (c->buflen == 64) {
            sha256_block(c, c->buf);
            c->buflen = 0;
        }
    }
}

void ocire_sha256_final(ocire_sha256_ctx *c, u8 out[32]) {
    u64 bits = c->len * 8;
    u8 pad = 0x80;
    ocire_sha256_update(c, &pad, 1);
    u8 zero = 0;
    while (c->buflen != 56) {
        ocire_sha256_update(c, &zero, 1);
    }
    u8 lenb[8];
    for (int i = 0; i < 8; i++) {
        lenb[i] = (u8)(bits >> (56 - i * 8));
    }
    /* This final update lands buflen exactly on 64 -> one block. */
    c->len -= 8;                       /* length bytes are not message */
    oc_memcpy(c->buf + 56, lenb, 8);
    sha256_block(c, c->buf);
    c->buflen = 0;
    for (int i = 0; i < 8; i++) {
        out[i*4+0] = (u8)(c->h[i] >> 24);
        out[i*4+1] = (u8)(c->h[i] >> 16);
        out[i*4+2] = (u8)(c->h[i] >> 8);
        out[i*4+3] = (u8)(c->h[i]);
    }
}

void ocire_sha256(const void *data, u64 len, u8 out[32]) {
    ocire_sha256_ctx c;
    ocire_sha256_init(&c);
    ocire_sha256_update(&c, data, len);
    ocire_sha256_final(&c, out);
}

void ocire_sha256_hex(const u8 digest[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i*2+0] = hex[digest[i] >> 4];
        out[i*2+1] = hex[digest[i] & 0xF];
    }
    out[64] = 0;
}

int ocire_sha256_eq(const u8 a[32], const u8 b[32]) {
    u8 d = 0;
    for (int i = 0; i < 32; i++) d |= a[i] ^ b[i];
    return d;                          /* 0 = equal */
}
