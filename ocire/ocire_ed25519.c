/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_ed25519.c
 * Purpose: Ed25519 verification - see ocire_ed25519.h.
 *
 *          Layering:
 *            fe25519   field GF(2^255-19), 5x51-bit limbs (donna-c64
 *                      style, unsigned __int128 intermediates)
 *            ge25519   twisted Edwards points in extended
 *                      coordinates (X:Y:Z:T), add-2008-hwcd-3 /
 *                      dbl-2008-hwcd from the EFD, point compression
 *                      and decompression with the standard sqrt(-1)
 *            verify    SHA-512(R||A||M) -> scalar, double-and-add
 *                      [S]B vs R + [k]A, encoded-point comparison
 */
#include "ocire_ed25519.h"
#include "ocire_sha512.h"
#include "ocire_util.h"

/* ---- fe25519: field GF(2^255 - 19) -------------------------------- */

#define MASK51 ((1ULL << 51) - 1)
typedef struct { u64 l[5]; } fe25519;
typedef unsigned __int128 u128;

static void fe_sub(fe25519 *r, const fe25519 *a, const fe25519 *b);
static void fe_carry(fe25519 *r);
static void fe_to_bytes(u8 out[32], const fe25519 *a);

static void fe_zero(fe25519 *a) { oc_memset(a, 0, sizeof(*a)); }
static void fe_one(fe25519 *a)  { fe_zero(a); a->l[0] = 1; }
static void fe_copy(fe25519 *r, const fe25519 *a) {
    oc_memcpy(r, a, sizeof(*r));
}
static int  fe_eq(const fe25519 *a, const fe25519 *b) {
    /* limbs are NOT uniquely reduced (donna style allows small
     * overshoots), so subtract, fully reduce and test for zero */
    fe25519 t;
    fe_sub(&t, a, b);
    fe_carry(&t);
    u8 z[32];
    fe_to_bytes(z, &t);
    for (int i = 0; i < 32; i++)
        if (z[i]) return 0;
    return 1;
}
static void fe_neg(fe25519 *r, const fe25519 *a) {
    /* -a = 0 - a: fold through 2p to stay non-negative */
    static const u64 two_p[5] = {
        0xFFFFFFFFFFFDAULL, 0xFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFEULL,
        0xFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFEULL
    };
    for (int i = 0; i < 5; i++) r->l[i] = two_p[i] - a->l[i];
}

static u64 fe_load8(const u8 *p) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) v |= (u64)p[i] << (8 * i);
    return v;
}

static void fe_from_bytes(fe25519 *r, const u8 in[32]) {
    r->l[0] = fe_load8(in + 0) & MASK51;
    r->l[1] = (fe_load8(in + 6) >> 3) & MASK51;
    r->l[2] = (fe_load8(in + 12) >> 6) & MASK51;
    r->l[3] = (fe_load8(in + 19) >> 1) & MASK51;
    r->l[4] = (fe_load8(in + 24) >> 12) & MASK51;
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
        t.l[i] &= MASK51;
    }
    t.l[4] &= MASK51;
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
    /* r = a + 2p - b keeps every limb non-negative */
    static const u64 two_p[5] = {
        0xFFFFFFFFFFFDAULL, 0xFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFEULL,
        0xFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFEULL
    };
    for (int i = 0; i < 5; i++)
        r->l[i] = a->l[i] + two_p[i] - b->l[i];
}

static void fe_carry(fe25519 *r) {
    u64 carry = 0;
    for (int i = 0; i < 5; i++) {
        u64 v = r->l[i] + carry;
        r->l[i] = v & MASK51;
        carry = v >> 51;
    }
    r->l[0] += carry * 19;
    carry = r->l[0] >> 51;
    r->l[0] &= MASK51;
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
    c = (u64)(t[0] >> 51); r->l[0] = (u64)t[0] & MASK51;
    t[1] += c;
    c = (u64)(t[1] >> 51); r->l[1] = (u64)t[1] & MASK51;
    t[2] += c;
    c = (u64)(t[2] >> 51); r->l[2] = (u64)t[2] & MASK51;
    t[3] += c;
    c = (u64)(t[3] >> 51); r->l[3] = (u64)t[3] & MASK51;
    t[4] += c;
    c = (u64)(t[4] >> 51); r->l[4] = (u64)t[4] & MASK51;
    r->l[0] += c * 19;
    c = r->l[0] >> 51;
    r->l[0] &= MASK51;
    r->l[1] += c;
}

static void fe_sqr(fe25519 *r, const fe25519 *a) { fe_mul(r, a, a); }

/* a^(p-2) with p-2 = 2^255 - 21 (0x7fff...ffeb), square-and-multiply */
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

/* decode constants from little-endian byte tables */
static void fe_from_le32(fe25519 *r, const u8 v[32]) {
    fe_from_bytes(r, v);
}

/* ---- ge25519: twisted Edwards points ------------------------------ */
/* Extended coordinates (X:Y:Z:T): x = X/Z, y = Y/Z, T = XY/Z.
 * Curve: -x^2 + y^2 = 1 + d x^2 y^2  (d = -121665/121666 mod p)    */

typedef struct { fe25519 X, Y, Z, T; } ge25519;

/* d = 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3
 * (little-endian; every constant table below is machine-generated
 * from the reference arithmetic - hand-typing 32-byte constants
 * produced nibble-swapped bytes, caught by the RFC 8032 vectors) */
static const u8 ED_D_LE[32] = {
    0xA3, 0x78, 0x59, 0x13, 0xCA, 0x4D, 0xEB, 0x75,
    0xAB, 0xD8, 0x41, 0x41, 0x4D, 0x0A, 0x70, 0x00,
    0x98, 0xE8, 0x79, 0x77, 0x79, 0x40, 0xC7, 0x8C,
    0x73, 0xFE, 0x6F, 0x2B, 0xEE, 0x6C, 0x03, 0x52
};
/* sqrt(-1) = 0x2b8324804fc1df0b2b4d00993dfbd7a72f431806ad2fe478c4ee1b274a0ea0b0 */
static const u8 ED_SQRTM1_LE[32] = {
    0xB0, 0xA0, 0x0E, 0x4A, 0x27, 0x1B, 0xEE, 0xC4,
    0x78, 0xE4, 0x2F, 0xAD, 0x06, 0x18, 0x43, 0x2F,
    0xA7, 0xD7, 0xFB, 0x3D, 0x99, 0x00, 0x4D, 0x2B,
    0x0B, 0xDF, 0xC1, 0x4F, 0x80, 0x24, 0x83, 0x2B
};
/* base point B (little-endian encodings) */
static const u8 ED_BY_LE[32] = {   /* y = 4/5 */
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};
static const u8 ED_BX_LE[32] = {   /* x, even sign */
    0x1A, 0xD5, 0x25, 0x8F, 0x60, 0x2D, 0x56, 0xC9,
    0xB2, 0xA7, 0x25, 0x95, 0x60, 0xC7, 0x2C, 0x69,
    0x5C, 0xDC, 0xD6, 0xFD, 0x31, 0xE2, 0xA4, 0xC0,
    0xFE, 0x53, 0x6E, 0xCD, 0xD3, 0x36, 0x69, 0x21
};
/* group order L = 2^252 + 27742317777372353535851937790883648493 */
static const u8 ED_L_LE[32] = {
    0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58,
    0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

static void ge_identity(ge25519 *p) {
    fe_zero(&p->X);
    fe_one(&p->Y);
    fe_one(&p->Z);
    fe_zero(&p->T);
}

static void ge_base(ge25519 *p) {
    fe_from_le32(&p->X, ED_BX_LE);
    fe_from_le32(&p->Y, ED_BY_LE);
    fe_one(&p->Z);
    fe_mul(&p->T, &p->X, &p->Y);
}

static void ge_tobytes(u8 out[32], const ge25519 *p) {
    fe25519 zinv, x, y;
    fe_invert(&zinv, &p->Z);
    fe_mul(&x, &p->X, &zinv);
    fe_mul(&y, &p->Y, &zinv);
    u8 b[32];
    fe_to_bytes(b, &y);
    /* x parity into the top bit of y */
    u8 xs[32];
    fe_to_bytes(xs, &x);
    b[31] = (u8)((b[31] & 0x7F) | (xs[0] & 1) << 7);
    oc_memcpy(out, b, 32);
}

/* Decompress a point.  Returns 0 on success, -1 when the encoding is
 * not a curve point (x^2 mismatch / negative zero). */
static int ge_frombytes(ge25519 *p, const u8 in[32]) {
    u8 yb[32];
    oc_memcpy(yb, in, 32);
    u8 sign = (u8)(yb[31] >> 7);
    yb[31] &= 0x7F;

    fe25519 y, d, one, u, v, xx, x, tx, t2, zinv;
    fe_from_bytes(&y, yb);
    fe_from_le32(&d, ED_D_LE);
    fe_one(&one);

    /* u = y^2 - 1, v = d y^2 + 1 */
    fe_sqr(&u, &y);
    fe_sub(&u, &u, &one);
    fe_carry(&u);
    fe_sqr(&v, &y);                                /* v = y^2     */
    fe_mul(&v, &d, &v);                            /* v = d y^2   */
    fe_add(&v, &v, &one);

    /* x = u v^3 (u v^7)^((p-5)/8)  -- the standard candidate */
    fe25519 v3, v7, t;
    fe_sqr(&v3, &v); fe_mul(&v3, &v3, &v);        /* v^3 */
    fe_sqr(&v7, &v3); fe_mul(&v7, &v7, &v);       /* v^7 */
    fe_mul(&xx, &u, &v3);
    fe_mul(&t, &u, &v7);
    /* exponent (p-5)/8 = 2^252 - 3 */
    {
        u8 e[32];
        oc_memset(e, 0xff, 32);
        e[0] = 0x0f;
        e[31] = 0xfd;                 /* 0x0fff...fffd */
        fe25519 acc;
        int started = 0;
        fe_zero(&acc);
        for (int i = 0; i < 256; i++) {
            int bit = (e[i / 8] >> (7 - (i % 8))) & 1;
            if (!started) {
                if (!bit) continue;
                started = 1;
                fe_copy(&acc, &t);
                continue;
            }
            fe_sqr(&acc, &acc);
            if (bit) fe_mul(&acc, &acc, &t);
        }
        fe_copy(&x, &acc);
    }
    fe_mul(&x, &x, &xx);

    /* check: v x^2 == u ?  else try x *= sqrt(-1) */
    fe_sqr(&tx, &x);
    fe_mul(&t2, &v, &tx);
    int ok1 = fe_eq(&t2, &u);
    if (!ok1) {
        fe25519 sqrtm1;
        fe_from_le32(&sqrtm1, ED_SQRTM1_LE);
        fe_mul(&x, &x, &sqrtm1);
        fe_sqr(&tx, &x);
        fe_mul(&t2, &v, &tx);
        if (!fe_eq(&t2, &u)) return -1;
    }

    /* x == 0 with sign set is not canonical */
    {
        u8 xb[32];
        fe_to_bytes(xb, &x);
        int is_zero = 1;
        for (int i = 0; i < 32; i++)
            if (xb[i]) { is_zero = 0; break; }
        if (is_zero && sign) return -1;
        if ((xb[0] & 1) != sign) fe_neg(&x, &x);
    }

    fe_copy(&p->X, &x);
    fe_copy(&p->Y, &y);
    fe_one(&p->Z);
    fe_mul(&p->T, &x, &y);
    (void)zinv;
    return 0;
}

/* point addition, add-2008-hwcd-3 (unified, works for doubling too) */
static void ge_add(ge25519 *r, const ge25519 *p, const ge25519 *q) {
    static fe25519 d2;                    /* 2*d, lazily initialized */
    static int d2_ready = 0;
    if (!d2_ready) {
        fe25519 d;
        fe_from_le32(&d, ED_D_LE);
        fe_add(&d2, &d, &d);
        fe_carry(&d2);
        d2_ready = 1;
    }
    fe25519 A, B, C, D, E, F, G, H, t0, t1;
    fe_sub(&t0, &p->Y, &p->X);            /* Y1-X1 */
    fe_sub(&t1, &q->Y, &q->X);            /* Y2-X2 */
    fe_mul(&A, &t0, &t1);
    fe_add(&t0, &p->Y, &p->X);            /* Y1+X1 */
    fe_add(&t1, &q->Y, &q->X);            /* Y2+X2 */
    fe_mul(&B, &t0, &t1);
    fe_mul(&t0, &p->T, &q->T);
    fe_mul(&C, &t0, &d2);
    fe_mul(&D, &p->Z, &q->Z);
    fe_add(&D, &D, &D);                   /* 2 Z1 Z2 */
    fe_sub(&E, &B, &A);  fe_carry(&E);
    fe_sub(&F, &D, &C);  fe_carry(&F);
    fe_add(&G, &D, &C);
    fe_add(&H, &B, &A);
    fe_mul(&r->X, &E, &F);
    fe_mul(&r->Y, &G, &H);
    fe_mul(&r->T, &E, &H);
    fe_mul(&r->Z, &F, &G);
}

/* scalar multiplication: r = [k]p for a 512-bit little-endian scalar
 * (MSB-first double-and-add over all 512 bits - the verification
 * scalar k = SHA-512(R||A||M) is only ever reduced implicitly by the
 * group order, and [k]A == [k mod L]A, so no explicit reduction is
 * needed; verification only - public scalars, no side-channel
 * requirement).  32-byte scalars simply zero-extend. */
static void ge_scalarmult(ge25519 *r, const u8 k_le[64], const ge25519 *p) {
    ge25519 acc, adder;
    ge_identity(&acc);
    adder = *p;
    for (int i = 511; i >= 0; i--) {
        ge25519 dbl;
        ge_add(&dbl, &acc, &acc);
        acc = dbl;
        int bit = (k_le[i / 8] >> (i & 7)) & 1;
        if (bit) {
            ge25519 sum;
            ge_add(&sum, &acc, &adder);
            acc = sum;
        }
    }
    *r = acc;
}

/* little-endian 32-byte compare: a < b ? -1 : a > b ? 1 : 0 */
static int le32_cmp(const u8 a[32], const u8 b[32]) {
    for (int i = 31; i >= 0; i--) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

/* ---- verification -------------------------------------------------- */

int ocire_ed25519_verify_pk(const u8 pk[32], const u8 *msg, u64 len,
                            const u8 sig[64]) {
    /* 1. decompress A and R */
    ge25519 A, R;
    if (ge_frombytes(&A, pk) != 0) return -2;      /* bad public key  */
    if (ge_frombytes(&R, sig) != 0) return -3;      /* bad R in sig    */

    /* 2. reject S >= L (non-canonical scalar) */
    if (le32_cmp(sig + 32, ED_L_LE) >= 0) return -4;

    /* 3. k = SHA-512(R || A || M), little-endian scalar */
    ocire_sha512_ctx_t c;
    u8 kb[64];
    ocire_sha512_init(&c);
    ocire_sha512_update(&c, sig, 32);               /* R */
    ocire_sha512_update(&c, pk, 32);                /* A */
    ocire_sha512_update(&c, msg, (int)len);         /* M */
    ocire_sha512_final(&c, kb);

    /* 4. [S]B  vs  R + [k]A  (both scalars zero-extended to 64 bytes) */
    ge25519 B, SB, kA, RHS;
    u8 s64[64], k64[64];
    oc_memset(s64, 0, 64);
    oc_memset(k64, 0, 64);
    oc_memcpy(s64, sig + 32, 32);
    oc_memcpy(k64, kb, 64);
    ge_base(&B);
    ge_scalarmult(&SB, s64, &B);
    ge_scalarmult(&kA, k64, &A);
    ge_add(&RHS, &R, &kA);

    u8 lhs[32], rhs[32];
    ge_tobytes(lhs, &SB);
    ge_tobytes(rhs, &RHS);
    if (oc_memcmp(lhs, rhs, 32) != 0) return -1;    /* equation failed */

    return 0;
}

int ocire_ed25519_verify(const u8 *msg, u64 len, const u8 sig[64]) {
    return ocire_ed25519_verify_pk(ocire_ed25519_pubkey, msg, len, sig);
}
