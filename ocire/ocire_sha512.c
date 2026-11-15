/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_sha512.c
 * Purpose: SHA-512 (FIPS 180-4), freestanding - see ocire_sha512.h.
 *          Standard compression function; the padding path feeds the
 *          final blocks directly so the length counter is not
 *          perturbed by the pad bytes.
 */
#include "ocire_sha512.h"
#include "ocire_util.h"

static const u64 K512[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static const u64 H512[8] = {
    0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL
};

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512_block(ocire_sha512_ctx_t *c, const u8 *p) {
    u64 w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((u64)p[i * 8] << 56) | ((u64)p[i * 8 + 1] << 48)
             | ((u64)p[i * 8 + 2] << 40) | ((u64)p[i * 8 + 3] << 32)
             | ((u64)p[i * 8 + 4] << 24) | ((u64)p[i * 8 + 5] << 16)
             | ((u64)p[i * 8 + 6] << 8) | (u64)p[i * 8 + 7];
    }
    for (int i = 16; i < 80; i++) {
        u64 s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        u64 s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u64 a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    u64 e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 80; i++) {
        u64 S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
        u64 ch = (e & f) ^ ((~e) & g);
        u64 t1 = h + S1 + ch + K512[i] + w[i];
        u64 S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
        u64 maj = (a & b) ^ (a & cc) ^ (b & cc);
        u64 t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void ocire_sha512_init(ocire_sha512_ctx_t *c) {
    for (int i = 0; i < 8; i++) c->h[i] = H512[i];
    c->len_hi = c->len_lo = 0;
    c->buf_len = 0;
}

void ocire_sha512_update(ocire_sha512_ctx_t *c, const u8 *data, int len) {
    if (len <= 0) return;
    u64 lo = c->len_lo + (u64)len;
    if (lo < c->len_lo) c->len_hi++;
    c->len_lo = lo;
    while (len > 0) {
        int take = 128 - c->buf_len;
        if (take > len) take = len;
        oc_memcpy(c->buf + c->buf_len, data, (u64)take);
        c->buf_len += take;
        data += take;
        len -= take;
        if (c->buf_len == 128) {
            sha512_block(c, c->buf);
            c->buf_len = 0;
        }
    }
}

void ocire_sha512_final(ocire_sha512_ctx_t *c, u8 out[64]) {
    u64 total_bits_hi = c->len_hi * 8 + (c->len_lo >> 61);
    u64 total_bits_lo = c->len_lo << 3;
    /* padding: 0x80 then zeros until 112 mod 128, then 16-byte length */
    u8 pad = 0x80;
    ocire_sha512_update(c, &pad, 1);
    u8 zero = 0;
    while (c->buf_len != 112) ocire_sha512_update(c, &zero, 1);
    u8 len_bytes[16];
    for (int i = 0; i < 8; i++) len_bytes[i] = (u8)(total_bits_hi >> (56 - 8 * i));
    for (int i = 0; i < 8; i++) len_bytes[8 + i] = (u8)(total_bits_lo >> (56 - 8 * i));
    /* careful: update() would re-count length; feed the block directly */
    oc_memcpy(c->buf + c->buf_len, len_bytes, 16);
    sha512_block(c, c->buf);
    c->buf_len = 0;
    for (int i = 0; i < 64; i++)
        out[i] = (u8)(c->h[i / 8] >> (56 - 8 * (i % 8)));
}

void ocire_sha512(const u8 *data, int len, u8 out[64]) {
    ocire_sha512_ctx_t c;
    ocire_sha512_init(&c);
    ocire_sha512_update(&c, data, len);
    ocire_sha512_final(&c, out);
}
