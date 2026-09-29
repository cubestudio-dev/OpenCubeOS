/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/crypto.c
 * Purpose: AES-128, SHA-256, HMAC-SHA-256, HKDF, DH modexp.
 * All implementations are from-scratch, freestanding (no libc).
 */
#include "crypto.h"
#include "string.h"
#include "timer.h"
#include "pmm.h"

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
static void aes128_key_expansion(const u8 key[16], u8 round_keys[176]) {
    oc_memcpy(round_keys, key, 16);
    u8 temp[4];
    for (int i = 16; i < 176; i += 4) {
        oc_memcpy(temp, round_keys + i - 4, 4);
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

/* AES-128 encrypt single block */
void aes128_encrypt_block(const u8 key[16], const u8 plaintext[16], u8 ciphertext[16]) {
    u8 round_keys[176];
    aes128_key_expansion(key, round_keys);

    u8 state[16];
    oc_memcpy(state, plaintext, 16);

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

    oc_memcpy(ciphertext, state, 16);
}

/* AES-128-CTR mode encrypt/decrypt (same operation) */
void aes128_ctr_encrypt(const u8 key[16], const u8 nonce[16], const u8 *in, int in_len, u8 *out) {
    u8 counter[16];
    u8 keystream[16];
    oc_memcpy(counter, nonce, 16);

    int offset = 0;
    while (offset < in_len) {
        aes128_encrypt_block(key, counter, keystream);
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

/* ============================================================
 * SHA-256 (FIPS-180-4)
 * ============================================================ */
static const u32 sha256_k[64] = {
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

void sha256(const u8 *data, int len, u8 hash[32]) {
    u32 h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    /* Padding: message + 0x80 + zeros + 8-byte length */
    int padded_len = ((len + 9 + 63) / 64) * 64;
    u8 *msg = (u8 *)(uintptr_t)pmm_alloc_frame(); /* use PMM for temp buffer */
    if (!msg) {
        /* Fallback: stack buffer for small messages */
        static u8 small_buf[1024];
        if (padded_len <= 1024) msg = small_buf;
        else { /* too large, truncate */ return; }
    }
    oc_memcpy(msg, data, len);
    msg[len] = 0x80;
    for (int i = len + 1; i < padded_len - 8; i++) msg[i] = 0;
    /* Append length in bits (big-endian 64-bit) */
    u64 bit_len = (u64)len * 8;
    for (int i = 0; i < 8; i++)
        msg[padded_len - 8 + i] = (u8)(bit_len >> (56 - 8 * i));

    /* Process each 512-bit block */
    for (int blk = 0; blk < padded_len; blk += 64) {
        u32 w[64];
        for (int i = 0; i < 16; i++) {
            int off = blk + i * 4;
            w[i] = ((u32)msg[off] << 24) | ((u32)msg[off+1] << 16) |
                   ((u32)msg[off+2] << 8) | (u32)msg[off+3];
        }
        for (int i = 16; i < 64; i++)
            w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

        u32 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            u32 t1 = hh + EP1(e) + CH(e,f,g) + sha256_k[i] + w[i];
            u32 t2 = EP0(a) + MAJ(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    /* Output big-endian */
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (u8)(h[i] >> 24);
        hash[i*4+1] = (u8)(h[i] >> 16);
        hash[i*4+2] = (u8)(h[i] >> 8);
        hash[i*4+3] = (u8)(h[i]);
    }

    if (padded_len > 1024) pmm_free_frame((u64)(uintptr_t)msg);
}

/* ============================================================
 * HMAC-SHA-256 (RFC 2104)
 * ============================================================ */
void hmac_sha256(const u8 *key, int key_len, const u8 *data, int data_len, u8 hmac_out[32]) {
    u8 k[64];
    if (key_len > 64) {
        sha256(key, key_len, k);
        for (int i = 32; i < 64; i++) k[i] = 0;
    } else {
        oc_memcpy(k, key, key_len);
        for (int i = key_len; i < 64; i++) k[i] = 0;
    }

    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

    /* Inner: H(ipad || data) */
    u8 *inner = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!inner) return;
    oc_memcpy(inner, ipad, 64);
    oc_memcpy(inner + 64, data, data_len);
    u8 inner_hash[32];
    sha256(inner, 64 + data_len, inner_hash);
    pmm_free_frame((u64)(uintptr_t)inner);

    /* Outer: H(opad || inner_hash) */
    u8 *outer = (u8 *)(uintptr_t)pmm_alloc_frame();
    if (!outer) return;
    oc_memcpy(outer, opad, 64);
    oc_memcpy(outer + 64, inner_hash, 32);
    sha256(outer, 64 + 32, hmac_out);
    pmm_free_frame((u64)(uintptr_t)outer);
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
        u8 *msg = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (!msg) return;
        int mlen = 0;
        if (prev_len > 0) { oc_memcpy(msg, prev, prev_len); mlen += prev_len; }
        if (info_len > 0) { oc_memcpy(msg + mlen, info, info_len); mlen += info_len; }
        msg[mlen++] = counter;
        hmac_sha256(prk, prk_len, msg, mlen, prev);
        pmm_free_frame((u64)(uintptr_t)msg);
        int copy = out_len - offset;
        if (copy > 32) copy = 32;
        oc_memcpy(out + offset, prev, copy);
        offset += copy;
        counter++;
        prev_len = 32;
    }
}

/* ============================================================
 * PRNG (not cryptographic, but sufficient for QEMU)
 * ============================================================ */
static u32 g_prng_state = 0;
void crypto_random(u8 *buf, int len) {
    if (g_prng_state == 0) g_prng_state = (u32)oc_timer_ticks() ^ 0xDEADBEEF;
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

/* DH group 1 prime (RFC 2409 Oakley Group 1) — 1024 bits = 128 bytes, big-endian */
const u8 dh_group1_prime[DH_BYTES] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC9,0x0F,0xDA,0xA2,0x21,0x68,0xC2,0x34,
    0xC4,0xC6,0x62,0x8B,0x80,0xDC,0x1C,0xD1,0x29,0x02,0x4E,0x08,0x8A,0x67,0xCC,0x74,
    0x02,0x0B,0xBE,0xA6,0x3B,0x13,0x9B,0x22,0x51,0x4A,0x08,0x79,0x8E,0x34,0x04,0xDD,
    0xEF,0x95,0x19,0xB3,0xCD,0x3A,0x43,0x1B,0x30,0x2B,0x0A,0x6D,0xF2,0x5F,0x14,0x37,
    0x77,0x4F,0xE1,0x26,0xB7,0x4D,0x05,0x16,0xD0,0x17,0x70,0xE1,0xC8,0xC0,0xFF,0xB0,
    0xF4,0xE6,0x18,0xB6,0x56,0x4F,0xFE,0x2D,0xD4,0xAF,0xB7,0xF3,0xB2,0x9A,0xB8,0xA2,
    0x47,0x91,0xB8,0x17,0xF4,0xE1,0xAC,0x6B,0x7E,0x0D,0xC0,0xE3,0x7D,0x3B,0x0C,0x0E,
    0xE8,0x9F,0x3F,0x37,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
const u8 dh_group1_generator[1] = { 0x02 };

/* Big-integer helpers (DH_BYTES-byte = 1024-bit numbers) */

/* Compare: a < b → -1, a == b → 0, a > b → 1 */
static int bn_cmp(const u8 *a, const u8 *b, int len) {
    for (int i = 0; i < len; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

/* Subtract: a -= b (a >= b assumed), len bytes. Returns borrow. */
static u8 bn_sub(u8 *a, const u8 *b, int len) {
    int borrow = 0;
    for (int i = len - 1; i >= 0; i--) {
        int diff = (int)a[i] - b[i] - borrow;
        if (diff < 0) { diff += 256; borrow = 1; }
        else borrow = 0;
        a[i] = (u8)diff;
    }
    return (u8)borrow;
}

/* (WP-09 bn_mod rewrite: bn_shl1 helper removed — bn_mod now uses inline
 * shift that preserves overflow into a len+1 byte remainder buffer.) */

/* Modular reduction: result = a mod m
 *  - a is 2*len bytes (big-endian)
 *  - m is len bytes
 *  - result is len bytes
 * WP-09 fix: use len+1 byte remainder buffer so the shift left doesn't
 * lose the MSB (otherwise we'd compare 2m > m incorrectly when m's MSB
 * is 1, leading to wrong result like g^x mod p = 0). */
static void bn_mod(u8 *result, const u8 *a, const u8 *m, int len) {
    u8 temp[2 * DH_BYTES];
    oc_memcpy(temp, a, len * 2);

    /* rem is len+1 bytes (big-endian): rem[0] = MSB overflow, rem[1..len] = current remainder */
    u8 rem[DH_BYTES + 4];
    oc_memset(rem, 0, len + 1);

    for (int bit = 0; bit < len * 8 * 2; bit++) {
        /* shift rem left by 1, preserving overflow into rem[0] */
        int carry = 0;
        for (int i = len; i >= 0; i--) {
            int new_carry = rem[i] >> 7;
            rem[i] = (u8)((rem[i] << 1) | carry);
            carry = new_carry;
        }
        /* carry from rem[0] is discarded (it's overflow beyond len+1 bytes,
         * but since rem < 2m at this point, it's always 0 anyway). */

        /* Bring in next bit of a from MSB */
        int byte_idx = bit / 8;
        int bit_idx = 7 - (bit % 8);
        if (byte_idx < len * 2 && (temp[byte_idx] >> bit_idx) & 1) {
            rem[len] |= 1;  /* LSB of rem is at index len */
        }

        /* If rem >= m, subtract m.
         * rem is len+1 bytes, m is len bytes (aligned to rem[1..len]).
         * If rem[0] != 0, rem > m (because m fits in len bytes).
         * Else compare rem[1..len] vs m[0..len-1]. */
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
            /* subtract m (len bytes, aligned to rem[1..len]) */
            int borrow = 0;
            for (int i = len; i >= 1; i--) {
                int diff = (int)rem[i] - m[i - 1] - borrow;
                if (diff < 0) { diff += 256; borrow = 1; }
                else borrow = 0;
                rem[i] = (u8)diff;
            }
            /* borrow propagates into rem[0] (should always become 0) */
            rem[0] = (u8)((int)rem[0] - borrow);
        }
    }

    /* Copy rem[1..len] to result[0..len-1] */
    oc_memcpy(result, rem + 1, len);
}

/* Modular exponentiation: result = base^exp mod mod
 * Uses square-and-multiply. All DH_BYTES-byte (1024-bit) numbers.
 * WP-09: each modexp takes ~5-10s in QEMU (1024-bit). For TLS/SSH this is
 * acceptable as a one-time cost per session. */
void dh_modexp(const u8 base[DH_BYTES], const u8 exp[DH_BYTES],
               const u8 mod[DH_BYTES], u8 result[DH_BYTES]) {
    u8 r[DH_BYTES];
    oc_memset(r, 0, DH_BYTES);
    r[DH_BYTES - 1] = 1; /* r = 1 */

    u8 b[DH_BYTES];
    /* b = base; reduce if >= mod */
    oc_memcpy(b, base, DH_BYTES);
    if (bn_cmp(b, mod, DH_BYTES) >= 0) {
        bn_sub(b, mod, DH_BYTES);
    }

    u8 product[2 * DH_BYTES];
    u8 prod2[2 * DH_BYTES];

    /* Square-and-multiply: scan exp from MSB */
    for (int i = 0; i < DH_BYTES; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            /* r = r^2 mod mod */
            oc_memset(product, 0, 2 * DH_BYTES);
            /* Schoolbook multiply: r * r → 2*DH_BYTES-byte product */
            for (int j = DH_BYTES - 1; j >= 0; j--) {
                for (int k = DH_BYTES - 1; k >= 0; k--) {
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
            bn_mod(r, product, mod, DH_BYTES);

            /* If exp bit is set: r = r * b mod mod */
            if ((exp[i] >> bit) & 1) {
                oc_memset(prod2, 0, 2 * DH_BYTES);
                for (int j = DH_BYTES - 1; j >= 0; j--) {
                    for (int k = DH_BYTES - 1; k >= 0; k--) {
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
                bn_mod(r, prod2, mod, DH_BYTES);
            }
        }
    }

    oc_memcpy(result, r, DH_BYTES);
}
