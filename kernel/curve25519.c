/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/curve25519.c
 * Purpose: X25519 (RFC 7748) over Curve25519, field = GF(2^255 - 19).
 *
 * Field elements: 5 limbs of 51 bits, little-endian (donna-c64 style).
 * Multiplication uses unsigned __int128 intermediates. The Montgomery
 * ladder is constant-time (secret scalar). The final inversion uses the
 * square-and-multiply chain for exponent p-2 (2^255 - 21).
 */
#include "curve25519.h"
#include "string.h"

#define X25519_MASK51 ((1ULL << 51) - 1)

typedef struct { u64 l[5]; } fe25519;
typedef unsigned __int128 u128;

static void fe_zero(fe25519 *a) { oc_memset(a->l, 0, sizeof(a->l)); }

static void fe_copy(fe25519 *r, const fe25519 *a) {
    oc_memcpy(r->l, a->l, sizeof(a->l));
}

static void fe_from_bytes(fe25519 *r, const u8 in[32]) {
    r->l[0] = (*(const u64 *)(const void *)(in + 0)) & X25519_MASK51;
    r->l[1] = ((*(const u64 *)(const void *)(in + 6)) >> 3) & X25519_MASK51;
    r->l[2] = ((*(const u64 *)(const void *)(in + 12)) >> 6) & X25519_MASK51;
    r->l[3] = ((*(const u64 *)(const void *)(in + 19)) >> 1) & X25519_MASK51;
    r->l[4] = ((*(const u64 *)(const void *)(in + 24)) >> 12) & X25519_MASK51;
}

static void fe_to_bytes(u8 out[32], const fe25519 *a) {
    fe25519 t;
    fe_copy(&t, a);
    /* full reduction: q = roughly (t + 19) / 2^255 */
    u64 q = (t.l[0] + 19) >> 51;
    q = (t.l[1] + q) >> 51;
    q = (t.l[2] + q) >> 51;
    q = (t.l[3] + q) >> 51;
    q = (t.l[4] + q) >> 51;
    t.l[0] += 19 * q;
    for (int i = 0; i < 4; i++) {
        t.l[i + 1] += t.l[i] >> 51;
        t.l[i] &= X25519_MASK51;
    }
    t.l[4] &= X25519_MASK51;
    u64 o0 = t.l[0] | (t.l[1] << 51);
    u64 o1 = (t.l[1] >> 13) | (t.l[2] << 38);
    u64 o2 = (t.l[2] >> 26) | (t.l[3] << 25);
    u64 o3 = (t.l[3] >> 39) | (t.l[4] << 12);
    for (int i = 0; i < 8; i++) {
        out[i]      = (u8)(o0 >> (8 * i));
        out[8 + i]  = (u8)(o1 >> (8 * i));
        out[16 + i] = (u8)(o2 >> (8 * i));
        out[24 + i] = (u8)(o3 >> (8 * i));
    }
}

static void fe_add(fe25519 *r, const fe25519 *a, const fe25519 *b) {
    for (int i = 0; i < 5; i++) r->l[i] = a->l[i] + b->l[i];
}

static void fe_sub(fe25519 *r, const fe25519 *a, const fe25519 *b) {
    /* r = a + 2p - b: keeps every limb non-negative; carry() normalizes. */
    static const u64 two_p[5] = {
        0xFFFFFFFFFFFDAULL, 0xFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFEULL,
        0xFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFEULL
    };
    for (int i = 0; i < 5; i++)
        r->l[i] = a->l[i] + two_p[i] - b->l[i];
}

/* normalize: fold carries so limbs are < 2^51 and value < 2p */
static void fe_carry(fe25519 *r) {
    u64 carry = 0;
    for (int i = 0; i < 5; i++) {
        u64 v = r->l[i] + carry;
        r->l[i] = v & X25519_MASK51;
        carry = v >> 51;
    }
    r->l[0] += carry * 19;
    carry = r->l[0] >> 51;
    r->l[0] &= X25519_MASK51;
    r->l[1] += carry;
}

static void fe_mul(fe25519 *r, const fe25519 *a, const fe25519 *b) {
    u128 t[5];
    u64 a0 = a->l[0], a1 = a->l[1], a2 = a->l[2], a3 = a->l[3], a4 = a->l[4];
    u64 b0 = b->l[0], b1 = b->l[1], b2 = b->l[2], b3 = b->l[3], b4 = b->l[4];
    /* 19-fold folding of products above 2^255 */
    t[0] = (u128)a0 * b0 + (u128)(a1 * 19) * b4 + (u128)(a2 * 19) * b3
         + (u128)(a3 * 19) * b2 + (u128)(a4 * 19) * b1;
    t[1] = (u128)a0 * b1 + (u128)a1 * b0 + (u128)(a2 * 19) * b4
         + (u128)(a3 * 19) * b3 + (u128)(a4 * 19) * b2;
    t[2] = (u128)a0 * b2 + (u128)a1 * b1 + (u128)a2 * b0
         + (u128)(a3 * 19) * b4 + (u128)(a4 * 19) * b3;
    t[3] = (u128)a0 * b3 + (u128)a1 * b2 + (u128)a2 * b1 + (u128)a3 * b0
         + (u128)(a4 * 19) * b4;
    t[4] = (u128)a0 * b4 + (u128)a1 * b3 + (u128)a2 * b2 + (u128)a3 * b1
         + (u128)a4 * b0;

    u64 c;
    c = (u64)(t[0] >> 51); r->l[0] = (u64)t[0] & X25519_MASK51;
    t[1] += c;
    c = (u64)(t[1] >> 51); r->l[1] = (u64)t[1] & X25519_MASK51;
    t[2] += c;
    c = (u64)(t[2] >> 51); r->l[2] = (u64)t[2] & X25519_MASK51;
    t[3] += c;
    c = (u64)(t[3] >> 51); r->l[3] = (u64)t[3] & X25519_MASK51;
    t[4] += c;
    c = (u64)(t[4] >> 51); r->l[4] = (u64)t[4] & X25519_MASK51;
    r->l[0] += c * 19;
    c = r->l[0] >> 51;
    r->l[0] &= X25519_MASK51;
    r->l[1] += c;
}

static void fe_sqr(fe25519 *r, const fe25519 *a) { fe_mul(r, a, a); }

/* multiply by a24 = 121665 (RFC 7748, curve25519 ladder constant) */
static void fe_mul_a24(fe25519 *r, const fe25519 *a) {
    u128 t[5];
    u64 c;
    for (int i = 0; i < 5; i++) t[i] = (u128)a->l[i] * 121665;
    c = (u64)(t[0] >> 51); r->l[0] = (u64)t[0] & X25519_MASK51;
    t[1] += c;
    c = (u64)(t[1] >> 51); r->l[1] = (u64)t[1] & X25519_MASK51;
    t[2] += c;
    c = (u64)(t[2] >> 51); r->l[2] = (u64)t[2] & X25519_MASK51;
    t[3] += c;
    c = (u64)(t[3] >> 51); r->l[3] = (u64)t[3] & X25519_MASK51;
    t[4] += c;
    c = (u64)(t[4] >> 51); r->l[4] = (u64)t[4] & X25519_MASK51;
    r->l[0] += c * 19;
    c = r->l[0] >> 51;
    r->l[0] &= X25519_MASK51;
    r->l[1] += c;
}

static void fe_cswap(u32 swap, fe25519 *a, fe25519 *b) {
    u64 mask = (u64)0 - (u64)swap;   /* all-ones when swapping */
    for (int i = 0; i < 5; i++) {
        u64 t = mask & (a->l[i] ^ b->l[i]);
        a->l[i] ^= t;
        b->l[i] ^= t;
    }
}

/* a^(p-2) with p-2 = 2^255 - 21 (0x7fff...ffeb), square-and-multiply. */
static void fe_invert(fe25519 *r, const fe25519 *a) {
    u8 e[32];
    fe25519 acc;
    oc_memset(e, 0xff, 32);
    e[0] = 0x7f;
    e[31] = 0xeb;
    int started = 0;
    fe_zero(&acc);
    for (int i = 0; i < 256; i++) {
        int bit = (e[i / 8] >> (7 - (i % 8))) & 1;
        if (!started) {
            if (!bit) continue;
            started = 1;
            fe_copy(&acc, a);
            continue;
        }
        fe_sqr(&acc, &acc);
        if (bit) fe_mul(&acc, &acc, a);
    }
    fe_copy(r, &acc);
}

int x25519(u8 out[32], const u8 scalar[32], const u8 point[32]) {
    fe25519 x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t0, t1;
    u8 s[32];
    u32 swap = 0;

    oc_memcpy(s, scalar, 32);
    s[0] &= 248;
    s[31] &= 127;
    s[31] |= 64;

    fe_from_bytes(&x1, point);
    fe_zero(&x2);
    x2.l[0] = 1;
    fe_copy(&x3, &x1);
    fe_zero(&z3);
    z3.l[0] = 1;
    fe_zero(&z2);

    for (int pos = 254; pos >= 0; pos--) {
        u32 bit = (s[pos / 8] >> (pos & 7)) & 1;
        swap ^= bit;
        fe_cswap(swap, &x2, &x3);
        fe_cswap(swap, &z2, &z3);
        swap = bit;

        fe_add(&a, &x2, &z2);          /* A = x2 + z2 */
        fe_sqr(&aa, &a);               /* AA = A^2 */
        fe_sub(&b, &x2, &z2);          /* B = x2 - z2 */
        fe_carry(&b);
        fe_sqr(&bb, &b);               /* BB = B^2 */
        fe_sub(&e, &aa, &bb);          /* E = AA - BB */
        fe_carry(&e);
        fe_add(&c, &x3, &z3);          /* C = x3 + z3 */
        fe_sub(&d, &x3, &z3);          /* D = x3 - z3 */
        fe_carry(&d);
        fe_mul(&da, &d, &a);           /* DA = D * A */
        fe_mul(&cb, &c, &b);           /* CB = C * B */
        fe_add(&t0, &da, &cb);
        fe_sqr(&x3, &t0);              /* x3 = (DA + CB)^2 */
        fe_sub(&t1, &da, &cb);
        fe_carry(&t1);
        fe_sqr(&t1, &t1);
        fe_mul(&z3, &x1, &t1);         /* z3 = x1 * (DA - CB)^2 */
        fe_mul(&x2, &aa, &bb);         /* x2 = AA * BB */
        fe_mul_a24(&t0, &e);           /* a24 * E, a24 = 121665 */
        fe_add(&t1, &aa, &t0);         /* AA + a24*E */
        fe_mul(&z2, &e, &t1);          /* z2 = E * (AA + a24*E) */
    }
    fe_cswap(swap, &x2, &x3);
    fe_cswap(swap, &z2, &z3);

    fe25519 zinv;
    fe_invert(&zinv, &z2);
    fe_mul(&t0, &x2, &zinv);
    fe_to_bytes(out, &t0);
    return 0;
}

int x25519_public(const u8 priv[32], u8 pub[32]) {
    static const u8 basepoint[32] = {9};   /* u = 9 */
    return x25519(pub, priv, basepoint);
}

int x25519_shared(const u8 priv[32], const u8 peer_pub[32], u8 shared[32]) {
    return x25519(shared, priv, peer_pub);
}
