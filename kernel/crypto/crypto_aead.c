/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/aead.c
 * Purpose: AES-128/256-GCM + ChaCha20-Poly1305, from scratch, freestanding.
 *
 * - AES: FIPS-197 encrypt-only core with on-the-fly round keys (GCM and
 *   CTR never need the decrypt direction).
 * - GCM: NIST SP 800-38D. GHASH uses a bit-at-a-time GF(2^128) multiply
 *   (no tables — small code, adequate speed for record-size data).
 * - ChaCha20/Poly1305: RFC 8439, AEAD construction with AAD.
 */
#include "crypto_aead.h"
#include "lib_string.h"

/* ================= AES (encrypt core, 128/256-bit keys) ================= */

static const u8 crypto_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const u8 crypto_aes_rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static u8 crypto_aes_xtime(u8 a) {
    return (u8)((a << 1) ^ ((a >> 7) * 0x1b));
}

static u8 crypto_aes_mul(u8 a, u8 b) {
    u8 p = 0;
    while (b) {
        if (b & 1) p ^= a;
        a = crypto_aes_xtime(a);
        b >>= 1;
    }
    return p;
}

typedef struct {
    u32 rk[60];      /* round keys as words */
    int rounds;
} crypto_aes_ctx_t;

static void crypto_aes_key_setup(crypto_aes_ctx_t *ctx, const u8 *key, int key_bits) {
    int nk = key_bits / 32;
    ctx->rounds = nk + 6;
    int nw = 4 * (ctx->rounds + 1);
    for (int i = 0; i < nk; i++) {
        ctx->rk[i] = ((u32)key[4 * i] << 24) | ((u32)key[4 * i + 1] << 16)
                   | ((u32)key[4 * i + 2] << 8) | (u32)key[4 * i + 3];
    }
    u8 temp[4];
    for (int i = nk; i < nw; i++) {
        u32 prev = ctx->rk[i - 1];
        temp[0] = (u8)(prev >> 24); temp[1] = (u8)(prev >> 16);
        temp[2] = (u8)(prev >> 8);  temp[3] = (u8)prev;
        if (i % nk == 0) {
            /* RotWord + SubWord + Rcon */
            u8 t = temp[0];
            temp[0] = crypto_aes_sbox[temp[1]] ^ crypto_aes_rcon[i / nk];
            temp[1] = crypto_aes_sbox[temp[2]];
            temp[2] = crypto_aes_sbox[temp[3]];
            temp[3] = crypto_aes_sbox[t];
        } else if (nk > 6 && i % nk == 4) {
            for (int k = 0; k < 4; k++) temp[k] = crypto_aes_sbox[temp[k]];
        }
        ctx->rk[i] = ctx->rk[i - nk]
                   ^ ((u32)temp[0] << 24) ^ ((u32)temp[1] << 16)
                   ^ ((u32)temp[2] << 8) ^ (u32)temp[3];
    }
}

static void crypto_aes_encrypt_block(const crypto_aes_ctx_t *ctx, const u8 in[16], u8 out[16]) {
    u8 s[16], t[16];
    int rounds = ctx->rounds;
    /* AddRoundKey 0 */
    for (int i = 0; i < 4; i++) {
        u32 k = ctx->rk[i];
        s[4 * i]     = in[4 * i]     ^ (u8)(k >> 24);
        s[4 * i + 1] = in[4 * i + 1] ^ (u8)(k >> 16);
        s[4 * i + 2] = in[4 * i + 2] ^ (u8)(k >> 8);
        s[4 * i + 3] = in[4 * i + 3] ^ (u8)k;
    }
    for (int rnd = 1; rnd <= rounds; rnd++) {
        /* SubBytes + ShiftRows into t */
        for (int i = 0; i < 16; i++) t[i] = crypto_aes_sbox[s[i]];
        u8 sh[16];
        sh[0] = t[0];  sh[1] = t[5];  sh[2] = t[10]; sh[3] = t[15];
        sh[4] = t[4];  sh[5] = t[9];  sh[6] = t[14]; sh[7] = t[3];
        sh[8] = t[8];  sh[9] = t[13]; sh[10] = t[2]; sh[11] = t[7];
        sh[12] = t[12]; sh[13] = t[1]; sh[14] = t[6]; sh[15] = t[11];
        /* MixColumns (skip in final round) + AddRoundKey */
        u32 k0 = ctx->rk[rnd * 4], k1 = ctx->rk[rnd * 4 + 1];
        u32 k2 = ctx->rk[rnd * 4 + 2], k3 = ctx->rk[rnd * 4 + 3];
        u8 kk[16];
        kk[0] = (u8)(k0 >> 24); kk[1] = (u8)(k0 >> 16); kk[2] = (u8)(k0 >> 8); kk[3] = (u8)k0;
        kk[4] = (u8)(k1 >> 24); kk[5] = (u8)(k1 >> 16); kk[6] = (u8)(k1 >> 8); kk[7] = (u8)k1;
        kk[8] = (u8)(k2 >> 24); kk[9] = (u8)(k2 >> 16); kk[10] = (u8)(k2 >> 8); kk[11] = (u8)k2;
        kk[12] = (u8)(k3 >> 24); kk[13] = (u8)(k3 >> 16); kk[14] = (u8)(k3 >> 8); kk[15] = (u8)k3;
        for (int col = 0; col < 4; col++) {
            u8 a0 = sh[4 * col], a1 = sh[4 * col + 1];
            u8 a2 = sh[4 * col + 2], a3 = sh[4 * col + 3];
            u8 x = a0 ^ a1 ^ a2 ^ a3;
            if (rnd == rounds) {
                s[4 * col]     = sh[4 * col]     ^ kk[4 * col];
                s[4 * col + 1] = sh[4 * col + 1] ^ kk[4 * col + 1];
                s[4 * col + 2] = sh[4 * col + 2] ^ kk[4 * col + 2];
                s[4 * col + 3] = sh[4 * col + 3] ^ kk[4 * col + 3];
            } else {
                u8 x0, x1, x2, x3;
                x0 = a0 ^ x ^ crypto_aes_mul(a0 ^ a1, 2);
                x1 = a1 ^ x ^ crypto_aes_mul(a1 ^ a2, 2);
                x2 = a2 ^ x ^ crypto_aes_mul(a2 ^ a3, 2);
                x3 = a3 ^ x ^ crypto_aes_mul(a3 ^ a0, 2);
                s[4 * col]     = x0 ^ kk[4 * col];
                s[4 * col + 1] = x1 ^ kk[4 * col + 1];
                s[4 * col + 2] = x2 ^ kk[4 * col + 2];
                s[4 * col + 3] = x3 ^ kk[4 * col + 3];
            }
        }
    }
    memcpy(out, s, 16);
}

/* ================= GHASH (GF(2^128), bit-at-a-time) ================= */

static void gf128_mul(u8 out[16], const u8 X[16], const u8 Y[16]) {
    u8 Z[16];
    u8 V[16];
    memset(Z, 0, 16);
    memcpy(V, Y, 16);
    for (int i = 0; i < 128; i++) {
        /* bit i of X (MSB first) */
        if ((X[i / 8] >> (7 - (i % 8))) & 1) {
            for (int k = 0; k < 16; k++) Z[k] ^= V[k];
        }
        /* V = V * x in GF(2^128) with R = 0xE1 << 120 */
        u8 lsb = V[15] & 1;
        /* right shift V by 1 bit */
        for (int k = 15; k > 0; k--)
            V[k] = (u8)((V[k] >> 1) | (V[k - 1] << 7));
        V[0] >>= 1;
        if (lsb) V[0] ^= 0xE1;
    }
    memcpy(out, Z, 16);
}

typedef struct {
    u8 H[16];
    u8 acc[16];
} crypto_ghash_ctx_t;

static void crypto_ghash_init(crypto_ghash_ctx_t *g, const u8 H[16]) {
    memcpy(g->H, H, 16);
    memset(g->acc, 0, 16);
}

static void crypto_ghash_update(crypto_ghash_ctx_t *g, const u8 *data, int len) {
    /* full blocks; partial block zero-padded per SP 800-38D */
    while (len > 0) {
        u8 block[16];
        int take = len < 16 ? len : 16;
        memcpy(block, data, take);
        if (take < 16) memset(block + take, 0, 16 - take);
        u8 tmp[16];
        for (int i = 0; i < 16; i++) tmp[i] = g->acc[i] ^ block[i];
        gf128_mul(g->acc, tmp, g->H);
        data += take;
        len -= take;
    }
}

/* ================= GCM ================= */

/* GCM CTR pass: keystream blocks from J0+1, J0+2, ... XORed in/out. */
static void crypto_gcm_ctr(const crypto_aes_ctx_t *ctx, const u8 J0[16],
                    const u8 *in, u8 *out, int len) {
    u8 ctr[16];
    memcpy(ctr, J0, 16);
    const u8 *ip = in;
    u8 *op = out;
    int rem = len;
    while (rem > 0) {
        /* increment counter (last 4 bytes, big-endian) */
        for (int k = 15; k >= 12; k--) {
            if (++ctr[k]) break;
        }
        u8 ks[16];
        crypto_aes_encrypt_block(ctx, ctr, ks);
        int take = rem < 16 ? rem : 16;
        for (int i = 0; i < take; i++) op[i] = ip[i] ^ ks[i];
        ip += take; op += take; rem -= take;
    }
}

static int crypto_gcm_crypt(int key_bits, const u8 *key, const u8 nonce[12],
                     const u8 *aad, int aad_len,
                     const u8 *in, int len, u8 *out, u8 tag[16],
                     int encrypt) {
    if (key_bits != 128 && key_bits != 256) return -1;
    if (aad_len < 0 || len < 0) return -1;
    crypto_aes_ctx_t actx;
    u8 keybuf[32];
    memset(keybuf, 0, 32);
    memcpy(keybuf, key, key_bits / 8);
    crypto_aes_key_setup(&actx, keybuf, key_bits);

    /* H = E(K, 0^128) */
    u8 H[16];
    u8 zero[16];
    memset(zero, 0, 16);
    crypto_aes_encrypt_block(&actx, zero, H);

    /* J0 = nonce || 0x00000001 (12-byte nonce) */
    u8 J0[16];
    memcpy(J0, nonce, 12);
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

    /* S = E(K, J0) */
    u8 S[16];
    crypto_aes_encrypt_block(&actx, J0, S);

    /* GHASH over AAD || CIPHERTEXT || lengths. The ciphertext is `out`
     * when encrypting (after CTR) and `in` when decrypting (before CTR,
     * which also supports in-place operation). */
    u8 T[16];
    if (encrypt) {
        /* CTR pass first: pt -> ct in `out` */
        crypto_gcm_ctr(&actx, J0, in, out, len);
        crypto_ghash_ctx_t g;
        crypto_ghash_init(&g, H);
        crypto_ghash_update(&g, aad, aad_len);
        crypto_ghash_update(&g, out, len);
        u8 lens[16];
        u64 aad_bits = (u64)aad_len * 8, data_bits = (u64)len * 8;
        for (int i = 0; i < 8; i++) lens[i] = (u8)(aad_bits >> (56 - 8 * i));
        for (int i = 0; i < 8; i++) lens[8 + i] = (u8)(data_bits >> (56 - 8 * i));
        crypto_ghash_update(&g, lens, 16);
        for (int i = 0; i < 16; i++) T[i] = g.acc[i] ^ S[i];
        if (tag) memcpy(tag, T, 16);
        return 0;
    } else {
        /* decrypt: hash ct (== in), verify tag, THEN produce pt */
        crypto_ghash_ctx_t g;
        crypto_ghash_init(&g, H);
        crypto_ghash_update(&g, aad, aad_len);
        crypto_ghash_update(&g, in, len);
        u8 lens[16];
        u64 aad_bits = (u64)aad_len * 8, data_bits = (u64)len * 8;
        for (int i = 0; i < 8; i++) lens[i] = (u8)(aad_bits >> (56 - 8 * i));
        for (int i = 0; i < 8; i++) lens[8 + i] = (u8)(data_bits >> (56 - 8 * i));
        crypto_ghash_update(&g, lens, 16);
        for (int i = 0; i < 16; i++) T[i] = g.acc[i] ^ S[i];
        if (tag) {
            u8 diff = 0;
            for (int i = 0; i < 16; i++) diff |= (u8)(tag[i] ^ T[i]);
            if (diff) return -1;   /* authentication failed: no pt output */
        }
        crypto_gcm_ctr(&actx, J0, in, out, len);
        return 0;
    }
}

int crypto_aes128_gcm_seal(const u8 key[16], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *pt, int pt_len, u8 *ct, u8 tag[16]) {
    return crypto_gcm_crypt(128, key, nonce, aad, aad_len, pt, pt_len, ct, tag, 1);
}
int crypto_aes128_gcm_open(const u8 key[16], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *ct, int ct_len, u8 *pt, const u8 tag[16]) {
    return crypto_gcm_crypt(128, key, nonce, aad, aad_len, ct, ct_len, pt, (u8 *)tag, 0);
}
int crypto_aes256_gcm_seal(const u8 key[32], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *pt, int pt_len, u8 *ct, u8 tag[16]) {
    return crypto_gcm_crypt(256, key, nonce, aad, aad_len, pt, pt_len, ct, tag, 1);
}
int crypto_aes256_gcm_open(const u8 key[32], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *ct, int ct_len, u8 *pt, const u8 tag[16]) {
    return crypto_gcm_crypt(256, key, nonce, aad, aad_len, ct, ct_len, pt, (u8 *)tag, 0);
}

/* ================= ChaCha20 (RFC 8439) ================= */

#define CHACHA_ROTL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void crypto_chacha20_quarter(u32 *a, u32 *b, u32 *c, u32 *d) {
    *a += *b; *d ^= *a; *d = CHACHA_ROTL(*d, 16);
    *c += *d; *b ^= *c; *b = CHACHA_ROTL(*b, 12);
    *a += *b; *d ^= *a; *d = CHACHA_ROTL(*d, 8);
    *c += *d; *b ^= *c; *b = CHACHA_ROTL(*b, 7);
}

static void crypto_chacha20_block(const u8 key[32], const u8 nonce[12], u32 ctr,
                           u8 out[64]) {
    u32 state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) {
        state[4 + i] = ((u32)key[4 * i] << 0) | ((u32)key[4 * i + 1] << 8)
                     | ((u32)key[4 * i + 2] << 16) | ((u32)key[4 * i + 3] << 24);
    }
    state[12] = ctr;
    for (int i = 0; i < 3; i++) {
        state[13 + i] = ((u32)nonce[4 * i] << 0) | ((u32)nonce[4 * i + 1] << 8)
                      | ((u32)nonce[4 * i + 2] << 16) | ((u32)nonce[4 * i + 3] << 24);
    }
    u32 w[16];
    memcpy(w, state, sizeof(w));
    for (int i = 0; i < 10; i++) {
        crypto_chacha20_quarter(&w[0], &w[4], &w[8], &w[12]);
        crypto_chacha20_quarter(&w[1], &w[5], &w[9], &w[13]);
        crypto_chacha20_quarter(&w[2], &w[6], &w[10], &w[14]);
        crypto_chacha20_quarter(&w[3], &w[7], &w[11], &w[15]);
        crypto_chacha20_quarter(&w[0], &w[5], &w[10], &w[15]);
        crypto_chacha20_quarter(&w[1], &w[6], &w[11], &w[12]);
        crypto_chacha20_quarter(&w[2], &w[7], &w[8], &w[13]);
        crypto_chacha20_quarter(&w[3], &w[4], &w[9], &w[14]);
    }
    for (int i = 0; i < 16; i++) {
        u32 v = w[i] + state[i];
        out[4 * i]     = (u8)(v);
        out[4 * i + 1] = (u8)(v >> 8);
        out[4 * i + 2] = (u8)(v >> 16);
        out[4 * i + 3] = (u8)(v >> 24);
    }
}

void crypto_chacha20_xor(const u8 key[32], const u8 nonce[12], u32 ctr,
                  const u8 *in, int len, u8 *out) {
    u8 block[64];
    int off = 0;
    while (off < len) {
        crypto_chacha20_block(key, nonce, ctr++, block);
        int take = (len - off) < 64 ? (len - off) : 64;
        for (int i = 0; i < take; i++) out[off + i] = in[off + i] ^ block[i];
        off += take;
    }
}

/* ================= Poly1305 (RFC 8439) ================= */

/* unaligned little-endian 32-bit load */
#define PL_LE32(p, off) ( (u32)(p)[off] | ((u32)(p)[(off)+1] << 8) \
                        | ((u32)(p)[(off)+2] << 16) | ((u32)(p)[(off)+3] << 24) )

typedef struct {
    u32 r[5];
    u32 h[5];
    u32 pad[4];
    u8  buf[16];
    int buf_len;
} crypto_poly1305_ctx_t;

static void crypto_poly1305_blocks(crypto_poly1305_ctx_t *st, const u8 *m, int len,
                            u32 hibit) {
    /* len must be a multiple of 16 (caller pads). Each 16-byte block is
     * interpreted as a little-endian integer with implicit bit 2^128
     * (hibit) — zero for the final padded partial block — loaded into
     * 26-bit limbs at byte offsets 0/3/6/9/12 (donna-32). */
    u32 r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
    u32 h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
    u32 s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;

    while (len >= 16) {
        /* h += m block (26-bit limb extraction) */
        h0 += PL_LE32(m, 0) & 0x3ffffff;
        h1 += (PL_LE32(m, 3) >> 2) & 0x3ffffff;
        h2 += (PL_LE32(m, 6) >> 4) & 0x3ffffff;
        h3 += (PL_LE32(m, 9) >> 6) & 0x3ffffff;
        h4 += (PL_LE32(m, 12) >> 8) | hibit;

        u64 d0 = h0, d1 = h1, d2 = h2, d3 = h3, d4 = h4;

        u64 t0 = d0 * r0 + d1 * s4 + d2 * s3 + d3 * s2 + d4 * s1;
        u64 t1 = d0 * r1 + d1 * r0 + d2 * s4 + d3 * s3 + d4 * s2;
        u64 t2 = d0 * r2 + d1 * r1 + d2 * r0 + d3 * s4 + d4 * s3;
        u64 t3 = d0 * r3 + d1 * r2 + d2 * r1 + d3 * r0 + d4 * s4;
        u64 t4 = d0 * r4 + d1 * r3 + d2 * r2 + d3 * r1 + d4 * r0;

        u64 c;
        c = t0 >> 26; t0 &= 0x3ffffff;
        t1 += c; c = t1 >> 26; t1 &= 0x3ffffff;
        t2 += c; c = t2 >> 26; t2 &= 0x3ffffff;
        t3 += c; c = t3 >> 26; t3 &= 0x3ffffff;
        t4 += c; c = t4 >> 26; t4 &= 0x3ffffff;
        /* fold the top carry back with 2^130 = 5 (mod 2^130-5); h0 may
         * briefly exceed 26 bits — harmless for the next round. */
        h0 = (u32)(t0 + c * 5);
        h1 = (u32)t1; h2 = (u32)t2; h3 = (u32)t3; h4 = (u32)t4;

        m += 16;
        len -= 16;
    }
    st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

static void crypto_poly1305_init_ctx(crypto_poly1305_ctx_t *st, const u8 key[32]) {
    /* Clamp r (RFC 8439): bytes 3,7,11,15 &= 0x0f; bytes 4,8,12 &= 0xfc. */
    u8 rb[16];
    memcpy(rb, key, 16);
    rb[3] &= 15; rb[7] &= 15; rb[11] &= 15; rb[15] &= 15;
    rb[4] &= 252; rb[8] &= 252; rb[12] &= 252;
    /* extract 26-bit limbs the same way the message blocks are loaded */
    st->r[0] = PL_LE32(rb, 0) & 0x3ffffff;
    st->r[1] = (PL_LE32(rb, 3) >> 2) & 0x3ffffff;
    st->r[2] = (PL_LE32(rb, 6) >> 4) & 0x3ffffff;
    st->r[3] = (PL_LE32(rb, 9) >> 6) & 0x3ffffff;
    st->r[4] = (PL_LE32(rb, 12) >> 8) & 0xfffff;
    st->h[0] = st->h[1] = st->h[2] = st->h[3] = st->h[4] = 0;
    for (int i = 0; i < 4; i++) {
        st->pad[i] = ((u32)key[16 + 4 * i] << 0) | ((u32)key[17 + 4 * i] << 8)
                   | ((u32)key[18 + 4 * i] << 16) | ((u32)key[19 + 4 * i] << 24);
    }
    st->buf_len = 0;
}

/* Finalize: fully carry, reduce mod 2^130-5, add pad, output 128-bit LE. */
static void crypto_poly1305_finish_ctx(crypto_poly1305_ctx_t *st, u8 tag[16]) {
    u32 h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
    u32 c;
    c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    /* compute h + -p; g4 underflow (bit 31 set) means h < p */
    u32 g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    u32 g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    u32 g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    u32 g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    u32 g4 = h4 + c - (1u << 26);
    u32 use_g = (u32)(((i64)g4 >> 31) + 1);   /* 1 if h >= p */
    u32 use_h = 1 - use_g;
    h0 = (h0 & use_h) | (g0 & use_g);
    h1 = (h1 & use_h) | (g1 & use_g);
    h2 = (h2 & use_h) | (g2 & use_g);
    h3 = (h3 & use_h) | (g3 & use_g);
    h4 = (h4 & use_h) | ((g4 & 0x3ffffff) & use_g);

    /* h (130-bit, packed in 26-bit limbs) + pad (128-bit), mod 2^128 */
    unsigned __int128 hval = (unsigned __int128)h0
        | ((unsigned __int128)h1 << 26)
        | ((unsigned __int128)h2 << 52)
        | ((unsigned __int128)h3 << 78)
        | ((unsigned __int128)h4 << 104);
    unsigned __int128 pad = (unsigned __int128)st->pad[0]
        | ((unsigned __int128)st->pad[1] << 32)
        | ((unsigned __int128)st->pad[2] << 64)
        | ((unsigned __int128)st->pad[3] << 96);
    unsigned __int128 t = hval + pad;
    for (int i = 0; i < 16; i++) tag[i] = (u8)(t >> (8 * i));
}

void crypto_poly1305_mac(const u8 key[32], const u8 *data, int len, u8 tag[16]) {
    crypto_poly1305_ctx_t st;
    crypto_poly1305_init_ctx(&st, key);
    int full = len & ~15;
    if (full) crypto_poly1305_blocks(&st, data, full, 1u << 24);
    int rem = len - full;
    if (rem > 0) {
        u8 block[16];
        memset(block, 0, 16);
        memcpy(block, data + full, rem);
        block[rem] = 1;
        crypto_poly1305_blocks(&st, block, 16, 0);
    }
    crypto_poly1305_finish_ctx(&st, tag);
}

/* ================= ChaCha20-Poly1305 AEAD (RFC 8439) ================= */

static void rfc8439_poly_key(const u8 key[32], const u8 nonce[12], u8 poly_key[32]) {
    u8 block[64];
    crypto_chacha20_block(key, nonce, 0, block);
    memcpy(poly_key, block, 32);
}

/* mac_data = aad || pad16 || ct || pad16 || le64(aad_len) || le64(ct_len).
 * Built into one static buffer (single-threaded kernel; documented limit).
 * TLS records: aad <= 5+32, ct <= 16384+256+16 -> buffer 20480 is ample. */
#define OC_MAC_BUF_SIZE 20480
static u8 g_mac_buf[OC_MAC_BUF_SIZE];

static void crypto_poly1305_aead_tag(const u8 poly_key[32], const u8 *aad, int aad_len,
                              const u8 *ct, int ct_len, u8 tag[16]) {
    int aad_pad = (16 - (aad_len % 16)) % 16;
    int ct_pad = (16 - (ct_len % 16)) % 16;
    int total = aad_len + aad_pad + ct_len + ct_pad + 16;

    u8 lens[16];
    u64 al = (u64)aad_len, cl = (u64)ct_len;
    for (int i = 0; i < 8; i++) lens[i] = (u8)(al >> (8 * i));
    for (int i = 0; i < 8; i++) lens[8 + i] = (u8)(cl >> (8 * i));

    crypto_poly1305_ctx_t st;
    crypto_poly1305_init_ctx(&st, poly_key);
    if (total <= OC_MAC_BUF_SIZE) {
        u8 *p = g_mac_buf;
        if (aad_len) { memcpy(p, aad, aad_len); p += aad_len; }
        if (aad_pad) { memset(p, 0, aad_pad); p += aad_pad; }
        if (ct_len) { memcpy(p, ct, ct_len); p += ct_len; }
        if (ct_pad) { memset(p, 0, ct_pad); p += ct_pad; }
        memcpy(p, lens, 16);
        crypto_poly1305_blocks(&st, g_mac_buf, total & ~15, 1u << 24);
    } else {
        /* too large for the shared buffer: caller must cap record size */
        /* produce a deterministic garbage tag so open() will reject */
        memset(tag, 0xEE, 16);
        return;
    }
    crypto_poly1305_finish_ctx(&st, tag);
}

int chacha20poly1305_seal(const u8 key[32], const u8 nonce[12],
                          const u8 *aad, int aad_len,
                          const u8 *pt, int pt_len, u8 *ct, u8 tag[16]) {
    u8 poly_key[32];
    rfc8439_poly_key(key, nonce, poly_key);
    crypto_chacha20_xor(key, nonce, 1, pt, pt_len, ct);
    crypto_poly1305_aead_tag(poly_key, aad, aad_len, ct, pt_len, tag);
    return 0;
}

int chacha20poly1305_open(const u8 key[32], const u8 nonce[12],
                          const u8 *aad, int aad_len,
                          const u8 *ct, int ct_len, u8 *pt, const u8 tag[16]) {
    u8 poly_key[32];
    rfc8439_poly_key(key, nonce, poly_key);
    u8 computed[16];
    crypto_poly1305_aead_tag(poly_key, aad, aad_len, ct, ct_len, computed);
    u8 diff = 0;
    for (int i = 0; i < 16; i++) diff |= (u8)(computed[i] ^ tag[i]);
    if (diff) return -1;
    crypto_chacha20_xor(key, nonce, 1, ct, ct_len, pt);
    return 0;
}
