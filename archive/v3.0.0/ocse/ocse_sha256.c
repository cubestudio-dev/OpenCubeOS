/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_sha256.c
 * Purpose: see ocse_sha256.h. FIPS-180-4 SHA-256, ported from the
 *   v1.0.0 kernel crypto core (kernel/crypto/crypto_core.c) with the
 *   OCSE naming; no external dependencies beyond lib_string memcpy.
 */
#include "ocse_sha256.h"
#include "lib_string.h"

static const u32 ocse_sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define S256_ROTR(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define S256_CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define S256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S256_EP0(x) (S256_ROTR(x,2) ^ S256_ROTR(x,13) ^ S256_ROTR(x,22))
#define S256_EP1(x) (S256_ROTR(x,6) ^ S256_ROTR(x,11) ^ S256_ROTR(x,25))
#define S256_SIG0(x) (S256_ROTR(x,7) ^ S256_ROTR(x,18) ^ ((x) >> 3))
#define S256_SIG1(x) (S256_ROTR(x,17) ^ S256_ROTR(x,19) ^ ((x) >> 10))

static void ocse_sha256_block(const u8 *p, u32 h[8]) {
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((u32)p[i*4] << 24) | ((u32)p[i*4+1] << 16) |
               ((u32)p[i*4+2] << 8) | (u32)p[i*4+3];
    for (int i = 16; i < 64; i++)
        w[i] = S256_SIG1(w[i-2]) + w[i-7] + S256_SIG0(w[i-15]) + w[i-16];
    u32 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = hh + S256_EP1(e) + S256_CH(e,f,g) + ocse_sha256_k[i] + w[i];
        u32 t2 = S256_EP0(a) + S256_MAJ(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

void ocse_sha256_init(ocse_sha256_ctx *c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->total = 0;
    c->buflen = 0;
}

void ocse_sha256_update(ocse_sha256_ctx *c, const void *data, int len) {
    const u8 *p = (const u8 *)data;
    c->total += (u64)len;
    if (c->buflen) {
        int take = 64 - (int)c->buflen;
        if (take > len) take = len;
        memcpy(c->buf + c->buflen, p, (usize)take);
        c->buflen += (u32)take; p += take; len -= take;
        if (c->buflen == 64) {
            ocse_sha256_block(c->buf, c->h);
            c->buflen = 0;
        }
    }
    while (len >= 64) {
        ocse_sha256_block(p, c->h);
        p += 64; len -= 64;
    }
    if (len > 0) {
        memcpy(c->buf, p, (usize)len);
        c->buflen = (u32)len;
    }
}

void ocse_sha256_final(ocse_sha256_ctx *c, u8 hash[32]) {
    u64 bit_len = c->total * 8;
    u8 pad[128];
    int rem = (int)c->buflen;
    memcpy(pad, c->buf, (usize)rem);
    pad[rem] = 0x80;
    int padded = (rem + 9 <= 64) ? 64 : 128;
    for (int i = rem + 1; i < padded - 8; i++) pad[i] = 0;
    for (int i = 0; i < 8; i++)
        pad[padded - 8 + i] = (u8)(bit_len >> (56 - 8 * i));
    ocse_sha256_block(pad, c->h);
    if (padded == 128) ocse_sha256_block(pad + 64, c->h);
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (u8)(c->h[i] >> 24);
        hash[i*4+1] = (u8)(c->h[i] >> 16);
        hash[i*4+2] = (u8)(c->h[i] >> 8);
        hash[i*4+3] = (u8)(c->h[i]);
    }
}

void ocse_sha256(const u8 *data, int len, u8 hash[32]) {
    ocse_sha256_ctx c;
    ocse_sha256_init(&c);
    if (len > 0) ocse_sha256_update(&c, data, len);
    ocse_sha256_final(&c, hash);
}

void ocse_sha256_hex(const u8 hash[32], char out[65]) {
    static const char hexd[16] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i*2]   = hexd[(hash[i] >> 4) & 0xF];
        out[i*2+1] = hexd[hash[i] & 0xF];
    }
    out[64] = 0;
}
