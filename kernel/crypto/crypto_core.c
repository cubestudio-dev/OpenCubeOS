/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/crypto.c
 * Purpose: AES-128, SHA-256, HMAC-SHA-256, HKDF, DH modexp.
 * All implementations are from-scratch, freestanding (no libc).
 */
#include "crypto_core.h"
#include "lib_string.h"
#include "core_timer.h"
#include "mem_pmm.h"

/* ============================================================
 * AES-128 (FIPS-197)
 * Based on the public-domain reference implementation.
 * ============================================================ */

/* S-box */
static const u8 sbox[256] = {
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

/* Inverse S-box (for AES decrypt). Built from sbox: inv_sbox[sbox[i]] = i. */
static u8 inv_sbox[256];
static int inv_sbox_init = 0;
static void ensure_inv_sbox(void) {
    if (inv_sbox_init) return;
    for (int i = 0; i < 256; i++) inv_sbox[sbox[i]] = (u8)i;
    inv_sbox_init = 1;
}

/* Rcon for key expansion */
static const u8 rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

/* GF(2^8) multiply */
static u8 gmul(u8 a, u8 b) {
    u8 p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        u8 hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

/* SubWord: apply S-box to each byte of a 4-byte word */
static void sub_word(u8 *w) {
    for (int i = 0; i < 4; i++) w[i] = sbox[w[i]];
}

/* RotWord: rotate 4 bytes left by 1 */
static void rot_word(u8 *w) {
    u8 t = w[0]; w[0] = w[1]; w[1] = w[2]; w[2] = w[3]; w[3] = t;
}

/* Key expansion: 128-bit key → 11 round keys (176 bytes) */
static void crypto_aes128_key_expansion(const u8 key[16], u8 round_keys[176]) {
    memcpy(round_keys, key, 16);
    u8 temp[4];
    for (int i = 16; i < 176; i += 4) {
        memcpy(temp, round_keys + i - 4, 4);
        if (i % 16 == 0) {
            rot_word(temp);
            sub_word(temp);
            temp[0] ^= rcon[i / 16];
        }
        for (int j = 0; j < 4; j++)
            round_keys[i + j] = round_keys[i - 16 + j] ^ temp[j];
    }
}

/* AddRoundKey */
static void add_round_key(u8 state[16], const u8 *rk) {
    for (int i = 0; i < 16; i++) state[i] ^= rk[i];
}

/* SubBytes */
static void sub_bytes(u8 state[16]) {
    for (int i = 0; i < 16; i++) state[i] = sbox[state[i]];
}

/* ShiftRows */
static void shift_rows(u8 s[16]) {
    u8 t;
    /* Row 1: shift left by 1 */
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    /* Row 2: shift left by 2 */
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    /* Row 3: shift left by 3 (= shift right by 1) */
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

/* MixColumns */
static void mix_columns(u8 s[16]) {
    for (int c = 0; c < 4; c++) {
        u8 *col = s + c * 4;
        u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
        col[1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
        col[2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
        col[3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
    }
}

/* InvSubBytes */
static void inv_sub_bytes(u8 state[16]) {
    ensure_inv_sbox();
    for (int i = 0; i < 16; i++) state[i] = inv_sbox[state[i]];
}

/* InvShiftRows (reverse of shift_rows) */
static void inv_shift_rows(u8 s[16]) {
    u8 t;
    /* Row 1: shift right by 1 (reverse of shift left by 1) */
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    /* Row 2: shift right by 2 (reverse of shift left by 2) */
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    /* Row 3: shift right by 3 = shift left by 1 (reverse of shift left by 3) */
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

/* InvMixColumns (matrix: [14,11,13,9; 9,14,11,13; 13,9,14,11; 11,13,9,14]) */
static void inv_mix_columns(u8 s[16]) {
    for (int c = 0; c < 4; c++) {
        u8 *col = s + c * 4;
        u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
        col[1] = gmul(a0, 9)  ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
        col[2] = gmul(a0, 13) ^ gmul(a1, 9)  ^ gmul(a2, 14) ^ gmul(a3, 11);
        col[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9)  ^ gmul(a3, 14);
    }
}

/* AES-128 encrypt single block */
void crypto_aes128_encrypt_block(const u8 key[16], const u8 plaintext[16], u8 ciphertext[16]) {
    u8 round_keys[176];
    crypto_aes128_key_expansion(key, round_keys);

    u8 state[16];
    memcpy(state, plaintext, 16);

    add_round_key(state, round_keys);
    for (int round = 1; round < 10; round++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_keys + round * 16);
    }
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_keys + 10 * 16);

    memcpy(ciphertext, state, 16);
}

/* AES-128 decrypt single block (WP-09: needed for TLS server-side records) */
void crypto_aes128_decrypt_block(const u8 key[16], const u8 ciphertext[16], u8 plaintext[16]) {
    u8 round_keys[176];
    crypto_aes128_key_expansion(key, round_keys);

    u8 state[16];
    memcpy(state, ciphertext, 16);

    /* Initial AddRoundKey with last round key */
    add_round_key(state, round_keys + 10 * 16);
    /* 9 rounds of inverse: InvShiftRows, InvSubBytes, AddRoundKey, InvMixColumns */
    for (int round = 9; round >= 1; round--) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, round_keys + round * 16);
        inv_mix_columns(state);
    }
    /* Final round: InvShiftRows, InvSubBytes, AddRoundKey (no InvMixColumns) */
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, round_keys);

    memcpy(plaintext, state, 16);
}

/* AES-128-CTR mode encrypt/decrypt (same operation) */
void crypto_aes128_ctr_encrypt(const u8 key[16], const u8 nonce[16], const u8 *in, int in_len, u8 *out) {
    u8 counter[16];
    u8 keystream[16];
    memcpy(counter, nonce, 16);

    int offset = 0;
    while (offset < in_len) {
        crypto_aes128_encrypt_block(key, counter, keystream);
        int chunk = in_len - offset;
        if (chunk > 16) chunk = 16;
        for (int i = 0; i < chunk; i++)
            out[offset + i] = in[offset + i] ^ keystream[i];
        offset += chunk;
        /* Increment counter (big-endian, last 4 bytes) */
        for (int i = 15; i >= 12; i--) {
            if (++counter[i] != 0) break;
        }
    }
}

/* AES-128-CBC encrypt (in_len must be multiple of 16).
 * Cipher[i] = AES_E(Plain[i] XOR Cipher[i-1]); Cipher[-1] = IV. */
void crypto_aes128_cbc_encrypt(const u8 key[16], const u8 iv[16], const u8 *in, int in_len, u8 *out) {
    u8 prev[16];
    u8 xored[16];
    u8 cipher[16];
    memcpy(prev, iv, 16);
    for (int off = 0; off < in_len; off += 16) {
        for (int i = 0; i < 16; i++) xored[i] = in[off + i] ^ prev[i];
        crypto_aes128_encrypt_block(key, xored, cipher);
        memcpy(out + off, cipher, 16);
        memcpy(prev, cipher, 16);
    }
}

/* AES-128-CBC decrypt (in_len must be multiple of 16).
 * Plain[i] = AES_D(Cipher[i]) XOR Cipher[i-1]; Cipher[-1] = IV. */
void crypto_aes128_cbc_decrypt(const u8 key[16], const u8 iv[16], const u8 *in, int in_len, u8 *out) {
    u8 prev[16];
    u8 cipher[16];
    u8 dec[16];
    memcpy(prev, iv, 16);
    for (int off = 0; off < in_len; off += 16) {
        memcpy(cipher, in + off, 16);  /* save cipher block (will be XORed) */
        crypto_aes128_decrypt_block(key, cipher, dec);
        for (int i = 0; i < 16; i++) out[off + i] = dec[i] ^ prev[i];
        memcpy(prev, cipher, 16);
    }
}

/* ============================================================
 * SHA-256 (FIPS-180-4)
 * ============================================================ */
static const u32 crypto_sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x) (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

/* ---- streaming SHA-256: no large scratch buffers, arbitrary len ---- */
static void crypto_sha256_block(const u8 *p, u32 h[8]) {
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((u32)p[i*4] << 24) | ((u32)p[i*4+1] << 16) |
               ((u32)p[i*4+2] << 8) | (u32)p[i*4+3];
    for (int i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];
    u32 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = hh + EP1(e) + CH(e,f,g) + crypto_sha256_k[i] + w[i];
        u32 t2 = EP0(a) + MAJ(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

void crypto_sha256_init(crypto_sha256_ctx *c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->total = 0;
    c->buflen = 0;
}

void crypto_sha256_update(crypto_sha256_ctx *c, const void *data, int len) {
    const u8 *p = (const u8 *)data;
    c->total += (u64)len;
    if (c->buflen) {
        int take = 64 - c->buflen;
        if (take > len) take = len;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take; p += take; len -= take;
        if (c->buflen == 64) {
            crypto_sha256_block(c->buf, c->h);
            c->buflen = 0;
        }
    }
    while (len >= 64) {
        crypto_sha256_block(p, c->h);
        p += 64; len -= 64;
    }
    if (len > 0) {
        memcpy(c->buf, p, len);
        c->buflen = len;
    }
}

void crypto_sha256_final(crypto_sha256_ctx *c, u8 hash[32]) {
    u64 bit_len = c->total * 8;
    u8 pad[128];
    int rem = c->buflen;
    memcpy(pad, c->buf, rem);
    pad[rem] = 0x80;
    int padded = (rem + 9 <= 64) ? 64 : 128;
    for (int i = rem + 1; i < padded - 8; i++) pad[i] = 0;
    for (int i = 0; i < 8; i++)
        pad[padded - 8 + i] = (u8)(bit_len >> (56 - 8 * i));
    crypto_sha256_block(pad, c->h);
    if (padded == 128) crypto_sha256_block(pad + 64, c->h);
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (u8)(c->h[i] >> 24);
        hash[i*4+1] = (u8)(c->h[i] >> 16);
        hash[i*4+2] = (u8)(c->h[i] >> 8);
        hash[i*4+3] = (u8)(c->h[i]);
    }
}

void sha256(const u8 *data, int len, u8 hash[32]) {
    crypto_sha256_ctx c;
    crypto_sha256_init(&c);
    if (len > 0) crypto_sha256_update(&c, data, len);
    crypto_sha256_final(&c, hash);
}

/* ============================================================
 * HMAC-SHA-256 (RFC 2104)
 * ============================================================ */
void crypto_hmac_sha256(const u8 *key, int key_len, const u8 *data, int data_len, u8 crypto_hmac_out[32]) {
    u8 k[64];
    if (key_len > 64) {
        sha256(key, key_len, k);
        for (int i = 32; i < 64; i++) k[i] = 0;
    } else {
        memcpy(k, key, key_len);
        for (int i = key_len; i < 64; i++) k[i] = 0;
    }

    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

    /* Inner: H(ipad || data) — streaming, no large scratch buffer */
    crypto_sha256_ctx c;
    u8 inner_hash[32];
    crypto_sha256_init(&c);
    crypto_sha256_update(&c, ipad, 64);
    if (data_len > 0) crypto_sha256_update(&c, data, data_len);
    crypto_sha256_final(&c, inner_hash);

    /* Outer: H(opad || inner_hash) */
    crypto_sha256_init(&c);
    crypto_sha256_update(&c, opad, 64);
    crypto_sha256_update(&c, inner_hash, 32);
    crypto_sha256_final(&c, crypto_hmac_out);
}

/* ============================================================
 * HKDF-Expand (RFC 5869)
 * ============================================================ */
void hkdf_expand(const u8 *prk, int prk_len, const u8 *info, int info_len, u8 *out, int out_len) {
    u8 counter = 1;
    int offset = 0;
    u8 prev[32];
    int prev_len = 0;
    while (offset < out_len) {
        /* T(i) = HMAC(prk, T(i-1) || info || counter) */
        u8 *msg = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
        if (!msg) return;
        int mlen = 0;
        if (prev_len > 0) { memcpy(msg, prev, prev_len); mlen += prev_len; }
        if (info_len > 0) { memcpy(msg + mlen, info, info_len); mlen += info_len; }
        msg[mlen++] = counter;
        crypto_hmac_sha256(prk, prk_len, msg, mlen, prev);
        mem_pmm_free_frame((u64)(uintptr_t)msg);
        int copy = out_len - offset;
        if (copy > 32) copy = 32;
        memcpy(out + offset, prev, copy);
        offset += copy;
        counter++;
        prev_len = 32;
    }
}

/* ============================================================
 * TLS 1.3 key schedule (RFC 8446 section 7.1) — shared by the
 * kernel TLS stack and host tests.
 * ============================================================ */
void net_tls13_ks_expand_label(const u8 *secret, int slen, const char *label,
                           const u8 *context, int ctx_len,
                           u8 *out, int out_len) {
    int label_len = 0;
    while (label[label_len]) label_len++;
    u8 info[2 + 1 + 32 + 1 + 64];
    int n = 0;
    info[n++] = (u8)(out_len >> 8);
    info[n++] = (u8)out_len;
    info[n++] = (u8)(6 + label_len);
    memcpy(info + n, "tls13 ", 6); n += 6;
    memcpy(info + n, label, label_len); n += label_len;
    info[n++] = (u8)ctx_len;
    if (ctx_len) memcpy(info + n, context, ctx_len);
    n += ctx_len;
    hkdf_expand(secret, slen, info, n, out, out_len);
}

void net_tls13_ks_derive_secret(const u8 *secret, const char *label,
                            const u8 *thash, int thash_len, u8 out[32]) {
    net_tls13_ks_expand_label(secret, 32, label, thash, thash_len, out, 32);
}

/* ============================================================
 * PRNG (not cryptographic, but sufficient for QEMU)
 * ============================================================ */
static u32 g_prng_state = 0;
void crypto_random(u8 *buf, int len) {
    if (g_prng_state == 0) g_prng_state = (u32)core_timer_ticks() ^ 0xDEADBEEF;
    for (int i = 0; i < len; i++) {
        g_prng_state ^= g_prng_state << 13;
        g_prng_state ^= g_prng_state >> 17;
        g_prng_state ^= g_prng_state << 5;
        buf[i] = (u8)(g_prng_state >> 24);
    }
}

/* ============================================================
 * DH big-integer (1024-bit modular exponentiation, Oakley Group 1)
 * Uses schoolbook multiplication — slow but correct.
 * WP-09: switched from 2048-bit (group 14) to 1024-bit (group 1) for speed.
 * ============================================================ */

/* DH group 14 prime (RFC 3526, 2048-bit = 256 bytes). Shared by ssh.c (KEX)
 * and kmain.c shell_cmd_dhtest (fixed-vector truth test) — single source of truth.
 * WP-09 FIX: previous value was corrupt from byte 64 on (transcription error,
 * 176+ wrong bytes) — root cause of the SSH K mismatch. Verified against
 * paramiko kex_group14.P (the exact peer we interop with). */
/* DH group 14 prime (RFC 3526, 2048-bit = 256 bytes). Shared by ssh.c (KEX)
 * and kmain.c shell_cmd_dhtest (fixed-vector truth test) — single source of truth.
 * WP-09 FIX: previous value was corrupt from byte 64 on (transcription error,
 * 176+ wrong bytes) — root cause of the SSH K mismatch. Verified against
 * paramiko kex_group14.P (the exact peer we interop with). */
const u8 crypto_dh_group14_prime[256] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC9,0x0F,0xDA,0xA2,0x21,0x68,0xC2,0x34,
    0xC4,0xC6,0x62,0x8B,0x80,0xDC,0x1C,0xD1,0x29,0x02,0x4E,0x08,0x8A,0x67,0xCC,0x74,
    0x02,0x0B,0xBE,0xA6,0x3B,0x13,0x9B,0x22,0x51,0x4A,0x08,0x79,0x8E,0x34,0x04,0xDD,
    0xEF,0x95,0x19,0xB3,0xCD,0x3A,0x43,0x1B,0x30,0x2B,0x0A,0x6D,0xF2,0x5F,0x14,0x37,
    0x4F,0xE1,0x35,0x6D,0x6D,0x51,0xC2,0x45,0xE4,0x85,0xB5,0x76,0x62,0x5E,0x7E,0xC6,
    0xF4,0x4C,0x42,0xE9,0xA6,0x37,0xED,0x6B,0x0B,0xFF,0x5C,0xB6,0xF4,0x06,0xB7,0xED,
    0xEE,0x38,0x6B,0xFB,0x5A,0x89,0x9F,0xA5,0xAE,0x9F,0x24,0x11,0x7C,0x4B,0x1F,0xE6,
    0x49,0x28,0x66,0x51,0xEC,0xE4,0x5B,0x3D,0xC2,0x00,0x7C,0xB8,0xA1,0x63,0xBF,0x05,
    0x98,0xDA,0x48,0x36,0x1C,0x55,0xD3,0x9A,0x69,0x16,0x3F,0xA8,0xFD,0x24,0xCF,0x5F,
    0x83,0x65,0x5D,0x23,0xDC,0xA3,0xAD,0x96,0x1C,0x62,0xF3,0x56,0x20,0x85,0x52,0xBB,
    0x9E,0xD5,0x29,0x07,0x70,0x96,0x96,0x6D,0x67,0x0C,0x35,0x4E,0x4A,0xBC,0x98,0x04,
    0xF1,0x74,0x6C,0x08,0xCA,0x18,0x21,0x7C,0x32,0x90,0x5E,0x46,0x2E,0x36,0xCE,0x3B,
    0xE3,0x9E,0x77,0x2C,0x18,0x0E,0x86,0x03,0x9B,0x27,0x83,0xA2,0xEC,0x07,0xA2,0x8F,
    0xB5,0xC5,0x5D,0xF0,0x6F,0x4C,0x52,0xC9,0xDE,0x2B,0xCB,0xF6,0x95,0x58,0x17,0x18,
    0x39,0x95,0x49,0x7C,0xEA,0x95,0x6A,0xE5,0x15,0xD2,0x26,0x18,0x98,0xFA,0x05,0x10,
    0x15,0x72,0x8E,0x5A,0x8A,0xAC,0xAA,0x68,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

/* DH group 1 prime (RFC 2409 Oakley Group 1) — 1024 bits = 128 bytes, big-endian */
const u8 crypto_dh_group1_prime[DH_BYTES] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC9,0x0F,0xDA,0xA2,0x21,0x68,0xC2,0x34,
    0xC4,0xC6,0x62,0x8B,0x80,0xDC,0x1C,0xD1,0x29,0x02,0x4E,0x08,0x8A,0x67,0xCC,0x74,
    0x02,0x0B,0xBE,0xA6,0x3B,0x13,0x9B,0x22,0x51,0x4A,0x08,0x79,0x8E,0x34,0x04,0xDD,
    0xEF,0x95,0x19,0xB3,0xCD,0x3A,0x43,0x1B,0x30,0x2B,0x0A,0x6D,0xF2,0x5F,0x14,0x37,
    0x77,0x4F,0xE1,0x26,0xB7,0x4D,0x05,0x16,0xD0,0x17,0x70,0xE1,0xC8,0xC0,0xFF,0xB0,
    0xF4,0xE6,0x18,0xB6,0x56,0x4F,0xFE,0x2D,0xD4,0xAF,0xB7,0xF3,0xB2,0x9A,0xB8,0xA2,
    0x47,0x91,0xB8,0x17,0xF4,0xE1,0xAC,0x6B,0x7E,0x0D,0xC0,0xE3,0x7D,0x3B,0x0C,0x0E,
    0xE8,0x9F,0x3F,0x37,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
const u8 crypto_dh_group1_generator[1] = { 0x02 };

/* Big-integer helpers (DH_BYTES-byte = 1024-bit numbers) */

/* Compare: a < b → -1, a == b → 0, a > b → 1 */
static int crypto_bn_cmp(const u8 *a, const u8 *b, int len) {
    for (int i = 0; i < len; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

/* Subtract: a -= b (a >= b assumed), len bytes. Returns borrow. */
static u8 crypto_bn_sub(u8 *a, const u8 *b, int len) {
    int borrow = 0;
    for (int i = len - 1; i >= 0; i--) {
        int diff = (int)a[i] - b[i] - borrow;
        if (diff < 0) { diff += 256; borrow = 1; }
        else borrow = 0;
        a[i] = (u8)diff;
    }
    return (u8)borrow;
}

/* (WP-09 crypto_bn_mod rewrite: crypto_bn_shl1 helper removed — crypto_bn_mod now uses inline
 * shift that preserves overflow into a len+1 byte remainder buffer.) */

/* Modular reduction: result = a mod m
 *  - a is 2*len bytes (big-endian)
 *  - m is len bytes
 *  - result is len bytes
 * WP-09 fix: use len+1 byte remainder buffer so the shift left doesn't
 * lose the MSB (otherwise we'd compare 2m > m incorrectly when m's MSB
 * is 1, leading to wrong result like g^x mod p = 0).
 * WP-09 SSH fix: temp buffer must be 2*len bytes (was hardcoded 2*DH_BYTES=256,
 * which overflowed when called with len=256 for SSH group14 2048-bit DH). */
static void crypto_bn_mod(u8 *result, const u8 *a, const u8 *m, int len) {
    /* Allocate buffers via mem_pmm_alloc_frame to support arbitrary len
     * (stack arrays would be too large for 256-byte = 2048-bit DH). */
    u8 *temp = (u8 *)(uintptr_t)mem_pmm_alloc_frame();  /* 2*len bytes */
    u8 *rem = (u8 *)(uintptr_t)mem_pmm_alloc_frame();    /* len+1 bytes */
    if (!temp || !rem) {
        if (temp) mem_pmm_free_frame((u64)(uintptr_t)temp);
        if (rem) mem_pmm_free_frame((u64)(uintptr_t)rem);
        return;
    }
    memcpy(temp, a, len * 2);
    memset(rem, 0, len + 1);

    for (int bit = 0; bit < len * 8 * 2; bit++) {
        /* shift rem left by 1, preserving overflow into rem[0] */
        int carry = 0;
        for (int i = len; i >= 0; i--) {
            int new_carry = rem[i] >> 7;
            rem[i] = (u8)((rem[i] << 1) | carry);
            carry = new_carry;
        }

        /* Bring in next bit of a from MSB */
        int byte_idx = bit / 8;
        int bit_idx = 7 - (bit % 8);
        if (byte_idx < len * 2 && (temp[byte_idx] >> bit_idx) & 1) {
            rem[len] |= 1;
        }

        /* If rem >= m, subtract m */
        int geq;
        if (rem[0] != 0) {
            geq = 1;
        } else {
            geq = 0;
            for (int i = 0; i < len; i++) {
                if (rem[1 + i] < m[i]) { geq = -1; break; }
                if (rem[1 + i] > m[i]) { geq = 1; break; }
            }
        }
        if (geq >= 0) {
            int borrow = 0;
            for (int i = len; i >= 1; i--) {
                int diff = (int)rem[i] - m[i - 1] - borrow;
                if (diff < 0) { diff += 256; borrow = 1; }
                else borrow = 0;
                rem[i] = (u8)diff;
            }
            rem[0] = (u8)((int)rem[0] - borrow);
        }
    }

    memcpy(result, rem + 1, len);
    mem_pmm_free_frame((u64)(uintptr_t)temp);
    mem_pmm_free_frame((u64)(uintptr_t)rem);
}

/* Modular exponentiation: result = base^exp mod mod
 * Uses square-and-multiply. All DH_BYTES-byte (1024-bit) numbers.
 * WP-09: each modexp takes ~5-10s in QEMU (1024-bit). For TLS/SSH this is
 * acceptable as a one-time cost per session. */
void crypto_dh_modexp(const u8 base[DH_BYTES], const u8 exp[DH_BYTES],
               const u8 mod[DH_BYTES], u8 result[DH_BYTES]) {
    crypto_dh_modexp_n(base, exp, mod, result, DH_BYTES);
}

/* Generic modular exponentiation with explicit length (in bytes).
 * Used by SSH for 2048-bit (256-byte) DH group 14.
 * Allocates work buffers via mem_pmm_alloc_frame to avoid stack overflow. */
void crypto_dh_modexp_n(const u8 *base, const u8 *exp, const u8 *mod, u8 *result, int len) {
    u8 *r = (u8 *)(uintptr_t)mem_pmm_alloc_frame();     /* len bytes */
    u8 *b = (u8 *)(uintptr_t)mem_pmm_alloc_frame();      /* len bytes */
    u8 *product = (u8 *)(uintptr_t)mem_pmm_alloc_frame(); /* 2*len bytes */
    u8 *prod2 = (u8 *)(uintptr_t)mem_pmm_alloc_frame();   /* 2*len bytes */
    if (!r || !b || !product || !prod2) {
        if (r) mem_pmm_free_frame((u64)(uintptr_t)r);
        if (b) mem_pmm_free_frame((u64)(uintptr_t)b);
        if (product) mem_pmm_free_frame((u64)(uintptr_t)product);
        if (prod2) mem_pmm_free_frame((u64)(uintptr_t)prod2);
        return;
    }

    memset(r, 0, len);
    r[len - 1] = 1;  /* r = 1 */

    /* b = base; reduce if >= mod */
    memcpy(b, base, len);
    if (crypto_bn_cmp(b, mod, len) >= 0) {
        crypto_bn_sub(b, mod, len);
    }

    /* Square-and-multiply: scan exp from MSB */
    for (int i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            /* r = r^2 mod mod */
            memset(product, 0, 2 * len);
            /* Schoolbook multiply: r * r → 2*len-byte product */
            for (int j = len - 1; j >= 0; j--) {
                for (int k = len - 1; k >= 0; k--) {
                    int prod_idx = j + k + 1;
                    u16 prod = (u16)r[j] * (u16)r[k];
                    int carry = prod;
                    for (int l = prod_idx; l >= 0 && carry; l--) {
                        int sum = product[l] + (carry & 0xFF);
                        product[l] = (u8)sum;
                        carry = (carry >> 8) + (sum >> 8);
                    }
                }
            }
            crypto_bn_mod(r, product, mod, len);

            /* If exp bit is set: r = r * b mod mod */
            if ((exp[i] >> bit) & 1) {
                memset(prod2, 0, 2 * len);
                for (int j = len - 1; j >= 0; j--) {
                    for (int k = len - 1; k >= 0; k--) {
                        int prod_idx = j + k + 1;
                        u16 prod = (u16)r[j] * (u16)b[k];
                        int carry = prod;
                        for (int l = prod_idx; l >= 0 && carry; l--) {
                            int sum = prod2[l] + (carry & 0xFF);
                            prod2[l] = (u8)sum;
                            carry = (carry >> 8) + (sum >> 8);
                        }
                    }
                }
                crypto_bn_mod(r, prod2, mod, len);
            }
        }
    }

    memcpy(result, r, len);
    mem_pmm_free_frame((u64)(uintptr_t)r);
    mem_pmm_free_frame((u64)(uintptr_t)b);
    mem_pmm_free_frame((u64)(uintptr_t)product);
    mem_pmm_free_frame((u64)(uintptr_t)prod2);
}
