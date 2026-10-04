/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/crypto_ec_nist.c
 * Purpose: NIST P-256 / P-384: Jacobian point arithmetic over the bn.c
 *          Montgomery core, ECDH, ECDSA verify.
 *
 * Both curves have a = -3, which the double/add formulas below use.
 * Field elements are EC_LIMBS-limb little-endian u64 arrays in the
 * Montgomery domain (xR mod p). Conversion to/from big-endian happens at
 * the API boundary only. The group-order domain (for ECDSA scalars) uses
 * its own Montgomery context on the same bn core.
 */
#include "crypto_ec_nist.h"
#include "crypto_bn.h"
#include "lib_string.h"
#include "crypto_core.h"   /* crypto_random */

#define EC_LIMBS 6
#define EC_BYTES (EC_LIMBS * 8)

typedef struct {
    int    nlimbs;      /* 4 for P-256, 6 for P-384 */
    int    size;        /* field size in bytes (32 / 48) */
    u64    p[EC_LIMBS];     /* prime (normal domain) */
    u64    am3[EC_LIMBS];   /* a = -3 in Montgomery domain */
    u64    b[EC_LIMBS];     /* curve b in Montgomery domain */
    u64    gx[EC_LIMBS];    /* generator in Montgomery domain */
    u64    gy[EC_LIMBS];
    u64    one[EC_LIMBS];   /* 1 in Montgomery domain */
    u64    n[EC_LIMBS];     /* group order (normal domain) */
    u64    n0p;             /* -p^-1 mod 2^64 */
    u64    n0n;             /* -n^-1 mod 2^64 */
    u64    p_rr[EC_LIMBS];  /* R^2 mod p */
    u64    n_rr[EC_LIMBS];  /* R^2 mod n */
    const u8 *prime_be;     /* prime, big-endian */
    const u8 *order_be;     /* order, big-endian */
    int    prime_len;
    int    order_len;
} crypto_ec_curve_t;

/* ---- Curve constants (big-endian, SEC2/FIPS 186-4 values) ---- */

static const u8 c_p256_p[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
static const u8 c_p256_b[32] = {
    0x5A,0xC6,0x35,0xD8,0xAA,0x3A,0x93,0xE7,0xB3,0xEB,0xBD,0x55,0x76,0x98,0x86,0xBC,
    0x65,0x1D,0x06,0xB0,0xCC,0x53,0xB0,0xF6,0x3B,0xCE,0x3C,0x3E,0x27,0xD2,0x60,0x4B
};
static const u8 c_p256_gx[32] = {
    0x6B,0x17,0xD1,0xF2,0xE1,0x2C,0x42,0x47,0xF8,0xBC,0xE6,0xE5,0x63,0xA4,0x40,0xF2,
    0x77,0x03,0x7D,0x81,0x2D,0xEB,0x33,0xA0,0xF4,0xA1,0x39,0x45,0xD8,0x98,0xC2,0x96
};
static const u8 c_p256_gy[32] = {
    0x4F,0xE3,0x42,0xE2,0xFE,0x1A,0x7F,0x9B,0x8E,0xE7,0xEB,0x4A,0x7C,0x0F,0x9E,0x16,
    0x2B,0xCE,0x33,0x57,0x6B,0x31,0x5E,0xCE,0xCB,0xB6,0x40,0x68,0x37,0xBF,0x51,0xF5
};
static const u8 c_p256_n[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51
};

static const u8 c_p384_p[48] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF
};
static const u8 c_p384_b[48] = {
    0xB3,0x31,0x2F,0xA7,0xE2,0x3E,0xE7,0xE4,0x98,0x8E,0x05,0x6B,0xE3,0xF8,0x2D,0x19,
    0x18,0x1D,0x9C,0x6E,0xFE,0x81,0x41,0x12,0x03,0x14,0x08,0x8F,0x50,0x13,0x87,0x5A,
    0xC6,0x56,0x39,0x8D,0x8A,0x2E,0xD1,0x9D,0x2A,0x85,0xC8,0xED,0xD3,0xEC,0x2A,0xEF
};
static const u8 c_p384_gx[48] = {
    0xAA,0x87,0xCA,0x22,0xBE,0x8B,0x05,0x37,0x8E,0xB1,0xC7,0x1E,0xF3,0x20,0xAD,0x74,
    0x6E,0x1D,0x3B,0x62,0x8B,0xA7,0x9B,0x98,0x59,0xF7,0x41,0xE0,0x82,0x54,0x2A,0x38,
    0x55,0x02,0xF2,0x5D,0xBF,0x55,0x29,0x6C,0x3A,0x54,0x5E,0x38,0x72,0x76,0x0A,0xB7
};
static const u8 c_p384_gy[48] = {
    0x36,0x17,0xDE,0x4A,0x96,0x26,0x2C,0x6F,0x5D,0x9E,0x98,0xBF,0x92,0x92,0xDC,0x29,
    0xF8,0xF4,0x1D,0xBD,0x28,0x9A,0x14,0x7C,0xE9,0xDA,0x31,0x13,0xB5,0xF0,0xB8,0xC0,
    0x0A,0x60,0xB1,0xCE,0x1D,0x7E,0x81,0x9D,0x7A,0x43,0x1D,0x7C,0x90,0xEA,0x0E,0x5F
};
static const u8 c_p384_n[48] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC7,0x63,0x4D,0x81,0xF4,0x37,0x2D,0xDF,
    0x58,0x1A,0x0D,0xB2,0x48,0xB0,0xA7,0x7A,0xEC,0xEC,0x19,0x6A,0xCC,0xC5,0x29,0x73
};

static crypto_ec_curve_t g_curves[2];
static int g_curves_init = 0;

/* ---- Field element ops (Montgomery domain, n limbs) ---- */

static void fe_to_mont(const crypto_ec_curve_t *c, u64 *r, const u64 *a) {
    crypto_bn_mont_mul(r, a, c->p_rr, c->p, c->nlimbs, c->n0p);
}

static void fe_from_be(const crypto_ec_curve_t *c, u64 *r, const u8 *in, int len) {
    u64 t[EC_LIMBS];
    crypto_bn_from_be(t, c->nlimbs, in, len);
    fe_to_mont(c, r, t);
}

static void fe_to_be(const crypto_ec_curve_t *c, u8 *out, int out_len, const u64 *a) {
    u64 one[EC_LIMBS], t[EC_LIMBS];
    crypto_bn_zero(one, c->nlimbs); one[0] = 1;
    crypto_bn_mont_mul(t, a, one, c->p, c->nlimbs, c->n0p);  /* out of Montgomery */
    crypto_bn_to_be(t, c->nlimbs, out, out_len);
}

static void fe_add(const crypto_ec_curve_t *c, u64 *r, const u64 *a, const u64 *b) {
    u64 carry = crypto_bn_add(r, a, b, c->nlimbs);
    if (carry || crypto_bn_cmp(r, c->p, c->nlimbs) >= 0)
        (void)crypto_bn_sub(r, r, c->p, c->nlimbs);   /* u64 wrap is intentional */
}

static void fe_sub(const crypto_ec_curve_t *c, u64 *r, const u64 *a, const u64 *b) {
    u64 borrow = crypto_bn_sub(r, a, b, c->nlimbs);
    if (borrow) (void)crypto_bn_add(r, r, c->p, c->nlimbs);
}

static void fe_mul(const crypto_ec_curve_t *c, u64 *r, const u64 *a, const u64 *b) {
    crypto_bn_mont_mul(r, a, b, c->p, c->nlimbs, c->n0p);
}

static void fe_sqr(const crypto_ec_curve_t *c, u64 *r, const u64 *a) {
    fe_mul(c, r, a, a);
}

/* Modular inverse, scalar domain: r = a^-1 mod p via Fermat (a^(p-2)).
 * a is a Montgomery-domain value; the exponentiation runs in the normal
 * domain so the result is a^-1*R (a proper Montgomery-domain inverse). */
static void fe_inv(const crypto_ec_curve_t *c, u64 *r, const u64 *a) {
    u8 pm2_be[EC_BYTES], out[EC_BYTES], a_norm[EC_BYTES];
    int sz = c->size;
    /* a -> normal domain */
    fe_to_be(c, a_norm, sz, a);
    /* exponent = p - 2 (single subtraction with borrow) */
    memcpy(pm2_be, c->prime_be, sz);
    int borrow = 2;
    for (int i = sz - 1; i >= 0 && borrow; i--) {
        int v = (int)pm2_be[i] - borrow;
        if (v < 0) { v += 256; borrow = 1; } else borrow = 0;
        pm2_be[i] = (u8)v;
    }
    if (crypto_bn_mod_exp(out, sz, a_norm, sz, pm2_be, sz, c->prime_be, sz) != 0)
        memset(out, 0, sz);
    /* back to Montgomery domain: inverse_m = inverse * R */
    u64 t[EC_LIMBS];
    crypto_bn_from_be(t, c->nlimbs, out, sz);
    crypto_bn_mont_mul(t, t, c->p_rr, c->p, c->nlimbs, c->n0p);
    crypto_bn_copy(r, t, c->nlimbs);
}

/* ---- Jacobian points ---- */

static void crypto_pt_copy(const crypto_ec_curve_t *c, u64 *X, u64 *Y, u64 *Z,
                    const u64 *x, const u64 *y, const u64 *z) {
    crypto_bn_copy(X, x, c->nlimbs);
    crypto_bn_copy(Y, y, c->nlimbs);
    crypto_bn_copy(Z, z, c->nlimbs);
}

static int crypto_pt_is_inf(const crypto_ec_curve_t *c, const u64 *Z) {
    return crypto_bn_is_zero(Z, c->nlimbs);
}

static void crypto_pt_set_inf(const crypto_ec_curve_t *c, u64 *X, u64 *Y, u64 *Z) {
    crypto_bn_zero(X, c->nlimbs);
    crypto_bn_zero(Y, c->nlimbs);
    crypto_bn_zero(Z, c->nlimbs);
    Y[0] = 1;
}

/* Doubling for a = -3. */
static void crypto_pt_double(const crypto_ec_curve_t *c,
                      u64 *X3, u64 *Y3, u64 *Z3,
                      const u64 *X1, const u64 *Y1, const u64 *Z1) {
    if (crypto_pt_is_inf(c, Z1) || crypto_bn_is_zero(Y1, c->nlimbs)) {
        crypto_pt_set_inf(c, X3, Y3, Z3);
        return;
    }
    u64 delta[EC_LIMBS], gamma[EC_LIMBS], beta[EC_LIMBS], alpha[EC_LIMBS];
    u64 t[EC_LIMBS], t2[EC_LIMBS];

    fe_sqr(c, delta, Z1);
    fe_sqr(c, gamma, Y1);
    fe_mul(c, beta, X1, gamma);
    fe_sub(c, t, X1, delta);
    fe_add(c, t2, X1, delta);
    fe_mul(c, alpha, t, t2);
    fe_add(c, t2, alpha, alpha);   /* t2 = 2*alpha */
    fe_add(c, alpha, alpha, t2);   /* alpha = 3*alpha (a = -3) */

    fe_sqr(c, X3, alpha);
    fe_add(c, t, beta, beta);
    fe_add(c, t, t, t);          /* 4*beta */
    fe_add(c, t2, t, t);         /* 8*beta */
    fe_sub(c, X3, X3, t2);

    fe_add(c, Z3, Y1, Z1);
    fe_sqr(c, Z3, Z3);
    fe_sub(c, Z3, Z3, gamma);
    fe_sub(c, Z3, Z3, delta);

    fe_sub(c, t, t, X3);         /* 4*beta - X3 */
    fe_mul(c, Y3, alpha, t);
    fe_sqr(c, gamma, gamma);
    fe_add(c, t, gamma, gamma);
    fe_add(c, t, t, t);          /* 4*gamma^2 */
    fe_add(c, t, t, t);          /* 8*gamma^2 */
    fe_sub(c, Y3, Y3, t);
}

/* General Jacobian addition (P3 = P1 + P2). */
static void crypto_pt_add(const crypto_ec_curve_t *c,
                   u64 *X3, u64 *Y3, u64 *Z3,
                   const u64 *X1, const u64 *Y1, const u64 *Z1,
                   const u64 *X2, const u64 *Y2, const u64 *Z2) {
    if (crypto_pt_is_inf(c, Z1)) { crypto_pt_copy(c, X3, Y3, Z3, X2, Y2, Z2); return; }
    if (crypto_pt_is_inf(c, Z2)) { crypto_pt_copy(c, X3, Y3, Z3, X1, Y1, Z1); return; }

    u64 Z1Z1[EC_LIMBS], Z2Z2[EC_LIMBS], U1[EC_LIMBS], U2[EC_LIMBS];
    u64 S1[EC_LIMBS], S2[EC_LIMBS], H[EC_LIMBS], r[EC_LIMBS];
    u64 HH[EC_LIMBS], HHH[EC_LIMBS], V[EC_LIMBS], t[EC_LIMBS];

    fe_sqr(c, Z1Z1, Z1);
    fe_sqr(c, Z2Z2, Z2);
    fe_mul(c, U1, X1, Z2Z2);
    fe_mul(c, U2, X2, Z1Z1);
    fe_mul(c, t, Z2, Z2Z2);
    fe_mul(c, S1, Y1, t);
    fe_mul(c, t, Z1, Z1Z1);
    fe_mul(c, S2, Y2, t);

    fe_sub(c, H, U2, U1);
    fe_sub(c, r, S2, S1);

    if (crypto_bn_is_zero(H, c->nlimbs)) {
        if (crypto_bn_is_zero(r, c->nlimbs)) {
            crypto_pt_double(c, X3, Y3, Z3, X1, Y1, Z1);
            return;
        }
        crypto_pt_set_inf(c, X3, Y3, Z3);
        return;
    }
    fe_sqr(c, HH, H);
    fe_mul(c, HHH, H, HH);
    fe_mul(c, V, U1, HH);

    fe_sqr(c, X3, r);
    fe_sub(c, X3, X3, HHH);
    fe_add(c, t, V, V);
    fe_sub(c, X3, X3, t);

    fe_sub(c, t, V, X3);
    fe_mul(c, Y3, r, t);
    fe_mul(c, t, S1, HHH);
    fe_sub(c, Y3, Y3, t);

    fe_mul(c, Z3, Z1, Z2);
    fe_mul(c, Z3, Z3, H);
}

/* Scalar multiplication with 4-bit windows (public-data path).
 * Windows are aligned from the first set bit: each window covers up to 4
 * bits starting at the current position. */
static void crypto_pt_scalar_mult(const crypto_ec_curve_t *c,
                           u64 *RX, u64 *RY, u64 *RZ,
                           const u8 *k_be, int k_len,
                           const u64 *PX, const u64 *PY, const u64 *PZ) {
    u64 tx[15][EC_LIMBS], ty[15][EC_LIMBS], tz[15][EC_LIMBS];
    crypto_pt_copy(c, tx[0], ty[0], tz[0], PX, PY, PZ);
    for (int i = 1; i < 15; i++) {
        /* tx[i] = (i+1)*P: odd index (i+1 even) via doubling of ((i+1)/2)P,
         * even index (i+1 odd) via addition of P to (i)P. */
        if (i & 1)
            crypto_pt_double(c, tx[i], ty[i], tz[i],
                      tx[(i + 1) / 2 - 1], ty[(i + 1) / 2 - 1], tz[(i + 1) / 2 - 1]);
        else
            crypto_pt_add(c, tx[i], ty[i], tz[i],
                   tx[i - 1], ty[i - 1], tz[i - 1], PX, PY, PZ);
    }

    int total = k_len * 8;
    crypto_pt_set_inf(c, RX, RY, RZ);
    int i = 0;
    while (i < total && !((k_be[i / 8] >> (7 - (i % 8))) & 1)) i++;
    int first = 1;
    while (i < total) {
        int wbits = (total - i) < 4 ? (total - i) : 4;
        int w = 0;
        for (int b = 0; b < wbits; b++)
            w = (w << 1) | ((k_be[(i + b) / 8] >> (7 - ((i + b) % 8))) & 1);
        if (first) {
            crypto_pt_copy(c, RX, RY, RZ, tx[w - 1], ty[w - 1], tz[w - 1]);
            first = 0;
        } else {
            for (int b = 0; b < wbits; b++)
                crypto_pt_double(c, RX, RY, RZ, RX, RY, RZ);
            if (w)
                crypto_pt_add(c, RX, RY, RZ, RX, RY, RZ,
                       tx[w - 1], ty[w - 1], tz[w - 1]);
        }
        i += wbits;
    }
}

/* Jacobian -> affine big-endian (x at out[0..sz), y at out[sz..2sz)). */
static int crypto_pt_to_affine_xy(const crypto_ec_curve_t *c, u8 *out, int out_len,
                           const u64 *X, const u64 *Y, const u64 *Z) {
    if (crypto_pt_is_inf(c, Z)) return -1;
    int sz = c->size;
    if (out_len < 2 * sz) return -1;
    u64 zi[EC_LIMBS], zi2[EC_LIMBS], zi3[EC_LIMBS], t[EC_LIMBS];
    fe_inv(c, zi, Z);
    fe_sqr(c, zi2, zi);
    fe_mul(c, zi3, zi2, zi);
    fe_mul(c, t, X, zi2);
    fe_to_be(c, out, sz, t);
    fe_mul(c, t, Y, zi3);
    fe_to_be(c, out + sz, sz, t);
    return 0;
}

/* Jacobian -> affine x only (for ECDH shared secrets). */
static int crypto_pt_to_affine_x(const crypto_ec_curve_t *c, u8 *out, int out_len,
                          const u64 *X, const u64 *Z) {
    if (crypto_pt_is_inf(c, Z)) return -1;
    int sz = c->size;
    if (out_len < sz) return -1;
    u64 zi[EC_LIMBS], zi2[EC_LIMBS], t[EC_LIMBS];
    fe_inv(c, zi, Z);
    fe_sqr(c, zi2, zi);
    fe_mul(c, t, X, zi2);
    fe_to_be(c, out, sz, t);
    return 0;
}

/* ---- Curve setup ---- */

/* Big-endian byte multiply: prod (2*len bytes) = a (len) * b (len).
 * Only used on public data (ECDSA scalar products). */
static void crypto_ec_mul_be(u8 *prod, const u8 *a, const u8 *b, int len) {
    memset(prod, 0, 2 * len);
    for (int i = 0; i < len; i++) {
        unsigned carry = 0;
        for (int j = 0; j < len; j++) {
            unsigned t = (unsigned)a[len - 1 - i] * b[len - 1 - j]
                         + prod[2 * len - 1 - (i + j)] + carry;
            prod[2 * len - 1 - (i + j)] = (u8)t;
            carry = t >> 8;
        }
        int idx = 2 * len - 1 - (i + len);
        while (carry && idx >= 0) {
            unsigned t = prod[idx] + carry;
            prod[idx] = (u8)t;
            carry = t >> 8;
            idx--;
        }
    }
}

static void curve_init(int id) {
    crypto_ec_curve_t *c = &g_curves[id];
    if (id == EC_P256) {
        c->nlimbs = 4; c->size = 32;
        c->prime_be = c_p256_p; c->order_be = c_p256_n;
        c->prime_len = 32; c->order_len = 32;
    } else {
        c->nlimbs = 6; c->size = 48;
        c->prime_be = c_p384_p; c->order_be = c_p384_n;
        c->prime_len = 48; c->order_len = 48;
    }
    crypto_bn_from_be(c->p, c->nlimbs, c->prime_be, c->prime_len);
    crypto_bn_from_be(c->n, c->nlimbs, c->order_be, c->order_len);
    c->n0p = crypto_bn_mont_n0inv(c->p, c->nlimbs);
    c->n0n = crypto_bn_mont_n0inv(c->n, c->nlimbs);
    crypto_bn_mont_rr(c->p_rr, c->p, c->nlimbs);
    crypto_bn_mont_rr(c->n_rr, c->n, c->nlimbs);
    u64 t[EC_LIMBS], three[EC_LIMBS];
    crypto_bn_zero(three, c->nlimbs); three[0] = 3;
    crypto_bn_sub(t, c->p, three, c->nlimbs);
    fe_to_mont(c, c->am3, t);
    fe_from_be(c, c->b, (id == EC_P256) ? c_p256_b : c_p384_b, c->size);
    fe_from_be(c, c->gx, (id == EC_P256) ? c_p256_gx : c_p384_gx, c->size);
    fe_from_be(c, c->gy, (id == EC_P256) ? c_p256_gy : c_p384_gy, c->size);
    crypto_bn_zero(c->one, c->nlimbs);
    c->one[0] = 1;
    fe_to_mont(c, c->one, c->one);
}

static crypto_ec_curve_t *get_curve(int id) {
    if (id != EC_P256 && id != EC_P384) return 0;
    if (!g_curves_init) {
        curve_init(EC_P256);
        curve_init(EC_P384);
        g_curves_init = 1;
    }
    return &g_curves[id];
}

/* ---- Public API ---- */

int crypto_ec_pub_valid(int curve, const u8 *pub, int pub_len) {
    crypto_ec_curve_t *c = get_curve(curve);
    if (!c) return -1;
    int sz = c->size;
    if (pub_len != 1 + 2 * sz || pub[0] != 0x04) return -1;
    u8 xb[EC_BYTES];
    u64 X[EC_LIMBS], Y[EC_LIMBS], x2[EC_LIMBS], x3[EC_LIMBS], lhs[EC_LIMBS], rhs[EC_LIMBS];
    fe_from_be(c, X, pub + 1, sz);
    fe_from_be(c, Y, pub + 1 + sz, sz);
    /* Range check: x, y < p. */
    fe_to_be(c, xb, sz, X);
    if (memcmp(xb, c->prime_be, sz) >= 0) return 0;
    fe_to_be(c, xb, sz, Y);
    if (memcmp(xb, c->prime_be, sz) >= 0) return 0;
    /* y^2 ?= x^3 - 3x + b */
    fe_sqr(c, lhs, Y);
    fe_sqr(c, x2, X);
    fe_mul(c, x3, x2, X);
    fe_mul(c, x2, X, c->am3);
    fe_add(c, rhs, x3, x2);
    fe_add(c, rhs, rhs, c->b);
    if (crypto_bn_cmp(lhs, rhs, c->nlimbs) != 0) return 0;
    return 1;
}

int crypto_ec_pub_from_priv(int curve, const u8 *priv, int priv_len,
                     u8 *pub_out, int pub_len) {
    crypto_ec_curve_t *c = get_curve(curve);
    if (!c) return -1;
    int sz = c->size;
    if (priv_len != sz || pub_len != 1 + 2 * sz) return -1;
    u64 QX[EC_LIMBS], QY[EC_LIMBS], QZ[EC_LIMBS];
    crypto_pt_scalar_mult(c, QX, QY, QZ, priv, sz, c->gx, c->gy, c->one);
    pub_out[0] = 0x04;   /* uncompressed point marker */
    return crypto_pt_to_affine_xy(c, pub_out + 1, pub_len - 1, QX, QY, QZ);
}

int crypto_ec_keygen(int curve, u8 *priv_out, int priv_len, u8 *pub_out, int pub_len) {
    crypto_ec_curve_t *c = get_curve(curve);
    if (!c) return -1;
    int sz = c->size;
    if (priv_len != sz || pub_len != 1 + 2 * sz) return -1;
    u8 d[EC_MAX_SCALAR];
    for (int tries = 0; tries < 64; tries++) {
        crypto_random(d, sz);
        d[sz - 1] |= 0x01;
        d[0] &= 0x7f;   /* keep < 2^(bits-1) < n cheaply, then verify */
        int ge = 0;
        for (int i = 0; i < sz; i++) {
            if (d[i] > c->order_be[i]) { ge = 1; break; }
            if (d[i] < c->order_be[i]) break;
        }
        if (ge) continue;
        memcpy(priv_out, d, sz);
        if (crypto_ec_pub_from_priv(curve, priv_out, priv_len, pub_out, pub_len) != 0)
            continue;
        return 0;
    }
    return -1;
}

int crypto_ec_ecdh(int curve, const u8 *priv, int priv_len,
            const u8 *peer_pub, int pub_len, u8 *shared_out, int shared_len) {
    crypto_ec_curve_t *c = get_curve(curve);
    if (!c) return -1;
    int sz = c->size;
    if (priv_len != sz || pub_len != 1 + 2 * sz || shared_len != sz) return -1;
    if (crypto_ec_pub_valid(curve, peer_pub, pub_len) != 1) return -1;
    u64 QX[EC_LIMBS], QY[EC_LIMBS], QZ[EC_LIMBS];
    fe_from_be(c, QX, peer_pub + 1, sz);
    fe_from_be(c, QY, peer_pub + 1 + sz, sz);
    crypto_pt_copy(c, QZ, QZ, QZ, c->one, c->one, c->one);
    u64 RX[EC_LIMBS], RY[EC_LIMBS], RZ[EC_LIMBS];
    crypto_pt_scalar_mult(c, RX, RY, RZ, priv, sz, QX, QY, QZ);
    if (crypto_pt_is_inf(c, RZ)) return -1;   /* invalid peer point */
    return crypto_pt_to_affine_x(c, shared_out, shared_len, RX, RZ);
}

int ecdsa_verify(int curve, const u8 *pub, int pub_len,
                 const u8 *hash, int hash_len,
                 const u8 *r, int r_len, const u8 *s, int s_len) {
    crypto_ec_curve_t *c = get_curve(curve);
    if (!c) return -1;
    int sz = c->size;
    if (r_len != sz || s_len != sz) return -1;
    if (pub_len != 1 + 2 * sz || pub[0] != 0x04) return -1;

    int nl = c->nlimbs;
    u64 Rv[EC_LIMBS], Sv[EC_LIMBS];
    crypto_bn_from_be(Rv, nl, r, sz);
    crypto_bn_from_be(Sv, nl, s, sz);
    /* 0 < r,s < n */
    if (crypto_bn_is_zero(Rv, nl) || crypto_bn_cmp(Rv, c->n, nl) >= 0) return 0;
    if (crypto_bn_is_zero(Sv, nl) || crypto_bn_cmp(Sv, c->n, nl) >= 0) return 0;

    /* s^-1 mod n via Fermat (n - 2, single subtraction with borrow). */
    u8 s_be[EC_BYTES], nm2_be[EC_BYTES], sinv_be[EC_BYTES];
    memcpy(s_be, s, sz);
    memcpy(nm2_be, c->order_be, sz);
    int borrow = 2;
    for (int i = sz - 1; i >= 0 && borrow; i--) {
        int v = (int)nm2_be[i] - borrow;
        if (v < 0) { v += 256; borrow = 1; } else borrow = 0;
        nm2_be[i] = (u8)v;
    }
    if (crypto_bn_mod_exp(sinv_be, sz, s_be, sz, nm2_be, sz, c->order_be, sz) != 0)
        return -1;

    /* u1 = e * s^-1 mod n ; u2 = r * s^-1 mod n (byte-domain multiply +
     * crypto_bn_mod_bytes reduction — no Montgomery-domain scalar arithmetic). */
    u8 u1_be[EC_BYTES], u2_be[EC_BYTES];
    u8 h_trunc[EC_BYTES];
    /* Truncate hash to the leftmost order bits when longer than n. */
    int hbytes = hash_len;
    if (hbytes > sz) hbytes = sz;
    memcpy(h_trunc, hash, hbytes);
    if (crypto_bn_mod_bytes(u1_be, sz, h_trunc, hbytes, c->order_be, sz) != 0) return -1;

    u8 prod[2 * EC_MAX_SCALAR];
    /* u1 = (e mod n) * sinv mod n */
    crypto_ec_mul_be(prod, u1_be, sinv_be, sz);
    if (crypto_bn_mod_bytes(u1_be, sz, prod, 2 * sz, c->order_be, sz) != 0) return -1;
    /* u2 = r * sinv mod n */
    crypto_ec_mul_be(prod, r, sinv_be, sz);
    if (crypto_bn_mod_bytes(u2_be, sz, prod, 2 * sz, c->order_be, sz) != 0) return -1;

    /* R = u1*G + u2*Q */
    u64 QX[EC_LIMBS], QY[EC_LIMBS];
    fe_from_be(c, QX, pub + 1, sz);
    fe_from_be(c, QY, pub + 1 + sz, sz);

    u64 A1[EC_LIMBS], A2[EC_LIMBS], A3[EC_LIMBS];
    u64 B1[EC_LIMBS], B2[EC_LIMBS], B3[EC_LIMBS];
    crypto_pt_scalar_mult(c, A1, A2, A3, u1_be, sz, c->gx, c->gy, c->one);
    crypto_pt_scalar_mult(c, B1, B2, B3, u2_be, sz, QX, QY, c->one);
    u64 SX[EC_LIMBS], SY[EC_LIMBS], SZ[EC_LIMBS];
    crypto_pt_add(c, SX, SY, SZ, A1, A2, A3, B1, B2, B3);
    if (crypto_pt_is_inf(c, SZ)) return 0;

    /* x_R mod n == r ? */
    u8 x_be[EC_BYTES];
    if (crypto_pt_to_affine_x(c, x_be, sz, SX, SZ) != 0) return 0;
    u8 x_mod[EC_BYTES];
    if (crypto_bn_mod_bytes(x_mod, sz, x_be, sz, c->order_be, sz) != 0) return -1;
    return memcmp(x_mod, r, sz) == 0;
}
