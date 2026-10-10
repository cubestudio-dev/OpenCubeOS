/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/bn.c
 * Purpose: Big-integer core: schoolbook multiply, Montgomery REDC, modexp.
 *
 * WP-09 history note: the original crypto_dh_modexp_n() used byte-wise
 * shift-subtract reduction, which needed 5-10s for a 1024-bit DH modexp and
 * would take minutes for RSA-4096 signature verification. This module uses
 * 64-bit limbs + Montgomery multiplication (REDC), making RSA-4096 verify
 * and NIST-curve point arithmetic practical in QEMU.
 *
 * REDC workspace sizing: the operand-scanning Montgomery reduction keeps the
 * running value below 4*B^(2n) (B = 2^64), so the accumulator uses 2n+2
 * limbs. All moduli used here (RSA moduli, NIST primes) have their top bit
 * set, which the R^2 precomputation relies on.
 *
 * Uses unsigned __int128 for 64x64->128 multiplies (gcc builtin for
 * x86_64, works in freestanding mode, no libc involved).
 *
 * Threading note (BUG-0294 / A4-07): crypto_bn_mod_exp used to run on three
 * file-static 576 B arrays, which made the whole bignum layer non-reentrant;
 * it now uses per-call stack workspace (see the note above the function), so
 * there is no shared state left in this file. Reentrancy is bounded by the
 * single-CPU kernel (an interrupt path may nest a crypto call safely); this
 * is NOT an SMP-safe claim - there is still no locking, by design.
 */
#include "crypto_bn.h"
#include "lib_string.h"

/* ---- Basic helpers ---- */

void crypto_bn_zero(u64 *r, int n) {
    for (int i = 0; i < n; i++) r[i] = 0;
}

void crypto_bn_copy(u64 *r, const u64 *a, int n) {
    for (int i = 0; i < n; i++) r[i] = a[i];
}

void crypto_bn_from_be(u64 *r, int n, const u8 *in, int len) {
    crypto_bn_zero(r, n);
    /* Fill from the least significant byte backwards. */
    for (int i = 0; i < len && i < 8 * n; i++) {
        u8 b = in[len - 1 - i];
        r[i / 8] |= (u64)b << ((i % 8) * 8);
    }
}

void crypto_bn_to_be(const u64 *a, int n, u8 *out, int out_len) {
    for (int i = 0; i < out_len; i++) {
        int idx = out_len - 1 - i;
        int li = idx / 8, bi = (idx % 8) * 8;
        out[i] = (li < n) ? (u8)(a[li] >> bi) : 0;
    }
}

int crypto_bn_cmp(const u64 *a, const u64 *b, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

int crypto_bn_is_zero(const u64 *a, int n) {
    u64 v = 0;
    for (int i = 0; i < n; i++) v |= a[i];
    return v == 0;
}

int crypto_bn_bitlen(const u64 *a, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (a[i]) {
            int bits = 64 * (i + 1);
            u64 v = a[i];
            while (!(v & 0x8000000000000000ULL)) { v <<= 1; bits--; }
            return bits;
        }
    }
    return 0;
}

u64 crypto_bn_add(u64 *r, const u64 *a, const u64 *b, int n) {
    u64 carry = 0;
    for (int i = 0; i < n; i++) {
        unsigned __int128 s = (unsigned __int128)a[i] + b[i] + carry;
        r[i] = (u64)s;
        carry = (u64)(s >> 64);
    }
    return carry;
}

u64 crypto_bn_sub(u64 *r, const u64 *a, const u64 *b, int n) {
    u64 borrow = 0;
    for (int i = 0; i < n; i++) {
        unsigned __int128 d = (unsigned __int128)a[i] - b[i] - borrow;
        r[i] = (u64)d;
        /* When the true result is negative, the u128 wrap makes the HIGH
         * 64 bits all ones: d = 2^128 + t (t < 0). Borrow out is therefore
         * bit 64 of d, NOT the sign bit of the low limb. */
        borrow = (u64)((d >> 64) & 1);
    }
    return borrow;
}

void crypto_bn_mul(u64 *r, const u64 *a, const u64 *b, int n) {
    crypto_bn_zero(r, 2 * n);
    for (int i = 0; i < n; i++) {
        u64 carry = 0;
        for (int j = 0; j < n; j++) {
            unsigned __int128 t =
                (unsigned __int128)a[i] * b[j] + r[i + j] + carry;
            r[i + j] = (u64)t;
            carry = (u64)(t >> 64);
        }
        r[i + n] = carry;
    }
}

/* ---- Montgomery ---- */

u64 crypto_bn_mont_n0inv(const u64 *m, int n) {
    (void)n;
    /* Newton iteration on x = m0^-1 mod 2^64, then negate.
     * Start from an inverse that is correct mod 2^4 (odd m -> odd inverse). */
    u64 x = 1;
    for (int i = 0; i < 8; i++) x *= (2 - m[0] * x);
    return (u64)(0 - x);
}

void crypto_bn_mont_rr(u64 *rr, const u64 *m, int n) {
    /* Compute R^2 mod m (R = 2^(64n)) by 128n safe doublings:
     * start from 1, each step rr = 2*rr mod m. Works for any m < R.
     * Invariant: rr < m before each step, so 2*rr < 2m — a single
     * conditional subtraction always restores the invariant (when the
     * doubling carries out, the true value is B^n + low < 2m, so
     * value - m < m and the wrapped subtraction is exact). */
    crypto_bn_zero(rr, n);
    rr[0] = 1;
    if (crypto_bn_cmp(rr, m, n) >= 0) (void)crypto_bn_sub(rr, rr, m, n);
    for (int i = 0; i < 2 * 64 * n; i++) {
        u64 c = crypto_bn_add(rr, rr, rr, n);   /* 2*rr */
        if (c) {
            (void)crypto_bn_sub(rr, rr, m, n);  /* wrapped result = value - m */
        } else if (crypto_bn_cmp(rr, m, n) >= 0) {
            (void)crypto_bn_sub(rr, rr, m, n);
        }
    }
}

void crypto_bn_mont_redc(u64 *out, const u64 *t, const u64 *m, int n, u64 n0inv) {
    /* Accumulator with 2n+2 limbs (intermediate < 4*B^(2n)). */
    u64 r[2 * BN_MAX_LIMBS + 2];
    int tn = 2 * n + 2;
    for (int i = 0; i < tn; i++) r[i] = (i < 2 * n) ? t[i] : 0;

    for (int i = 0; i < n; i++) {
        u64 k = r[i] * n0inv;
        u64 carry = 0;
        for (int j = 0; j < n; j++) {
            unsigned __int128 s =
                (unsigned __int128)k * m[j] + r[i + j] + carry;
            r[i + j] = (u64)s;
            carry = (u64)(s >> 64);
        }
        /* propagate the carry up (bounded: value < 4*B^(2n)) */
        int idx = i + n;
        while (carry && idx < tn) {
            unsigned __int128 s = (unsigned __int128)r[idx] + carry;
            r[idx] = (u64)s;
            carry = (u64)(s >> 64);
            idx++;
        }
    }
    /* Low n limbs are now zero: V = r/B^n, V < 3m. Reduce mod m. */
    u64 v[BN_MAX_LIMBS + 2];
    for (int i = 0; i <= n + 1; i++) v[i] = r[n + i];
    for (int k = 0; k < 4; k++) {
        if (v[n] == 0 && v[n + 1] == 0 && crypto_bn_cmp(v, m, n) < 0) break;
        u64 borrow = crypto_bn_sub(v, v, m, n);
        /* subtract borrow from the extension limbs */
        u64 ext = v[n];
        if (borrow) {
            unsigned __int128 s = (unsigned __int128)ext - 1;
            ext = (u64)s;
            if ((i64)s < 0) v[n + 1]--;
        }
        v[n] = ext;
    }
    crypto_bn_copy(out, v, n);
}

void crypto_bn_mont_mul(u64 *out, const u64 *a, const u64 *b,
                 const u64 *m, int n, u64 n0inv) {
    u64 t[2 * BN_MAX_LIMBS];
    crypto_bn_mul(t, a, b, n);
    crypto_bn_mont_redc(out, t, m, n, n0inv);
}

/* ---- modexp workspace ----
 * BUG-0294 (A4-07) FIX: these were three file-static 576 B arrays shared by
 * every crypto_bn_mod_exp call, making the whole bignum layer (RSA verify
 * in tls/ssh/x509, ECDSA scalar math) non-reentrant: an interleaved call
 * trampled the accumulator/base/modulus mid-exponentiation and could flip
 * verification results either way. They are now per-call locals (3 x 576 B
 * on the caller's stack, kthread stacks are 24 KiB); no cli window is
 * needed because there is no shared state left at all, and modexp runs
 * with interrupts enabled as before (a seconds-long cli window would stall
 * IRQ0 timekeeping). */

int crypto_bn_mod_exp(u8 *out, int out_len,
               const u8 *base, int base_len,
               const u8 *exp, int exp_len,
               const u8 *mod, int mod_len) {
    if (mod_len <= 0 || mod_len > BN_MAX_BYTES) return -1;
    if ((mod[mod_len - 1] & 1) == 0) return -1;   /* modulus must be odd */
    int n = (mod_len + 7) / 8;
    if (n > BN_MAX_LIMBS) return -1;

    u64 ws_a[BN_MAX_LIMBS], ws_b[BN_MAX_LIMBS], ws_m[BN_MAX_LIMBS];

    crypto_bn_from_be(ws_m, n, mod, mod_len);
    u64 n0 = crypto_bn_mont_n0inv(ws_m, n);

    u64 rr[BN_MAX_LIMBS], one[BN_MAX_LIMBS];
    crypto_bn_mont_rr(rr, ws_m, n);

    crypto_bn_from_be(ws_b, n, base, base_len);
    if (crypto_bn_cmp(ws_b, ws_m, n) >= 0) return -1;  /* base must be < m */
    crypto_bn_mont_mul(ws_b, ws_b, rr, ws_m, n, n0);   /* ws_b = base * R mod m */

    crypto_bn_zero(ws_a, n);
    crypto_bn_zero(one, n);
    one[0] = 1;
    crypto_bn_mont_mul(ws_a, one, rr, ws_m, n, n0);    /* ws_a = R mod m */

    /* MSB-first square-and-multiply. */
    int started = 0;
    for (int i = 0; i < exp_len; i++) {
        u8 byte = exp[i];
        for (int bit = 7; bit >= 0; bit--) {
            if (!started) {
                if (!((byte >> bit) & 1)) continue;
                started = 1;
                crypto_bn_copy(ws_a, ws_b, n);   /* A = base (Montgomery domain) */
                continue;
            }
            crypto_bn_mont_mul(ws_a, ws_a, ws_a, ws_m, n, n0);   /* square */
            if ((byte >> bit) & 1)
                crypto_bn_mont_mul(ws_a, ws_a, ws_b, ws_m, n, n0);
        }
    }
    if (!started) {
        /* exponent was zero: result = 1 mod m */
        crypto_bn_copy(ws_a, one, n);
        if (crypto_bn_cmp(ws_a, ws_m, n) >= 0) (void)crypto_bn_sub(ws_a, ws_a, ws_m, n);
    } else {
        /* convert out of the Montgomery domain: A * 1 -> A * R^-1 */
        crypto_bn_mont_mul(ws_a, ws_a, one, ws_m, n, n0);
    }
    if (out_len > mod_len) return -1;
    crypto_bn_to_be(ws_a, n, out, out_len);
    return 0;
}

/* Slow generic reduction (bit shift-subtract) — used only for scalar-bounds
 * reductions in ECDSA where inputs are at most 2x the modulus size. */
int crypto_bn_mod_bytes(u8 *out, int out_len, const u8 *x, int x_len,
                 const u8 *m, int m_len) {
    if (m_len <= 0 || m_len > BN_MAX_BYTES || x_len > BN_MAX_BYTES) return -1;
    int xn = (x_len + 7) / 8;
    int mn = (m_len + 7) / 8;
    int nn = (xn > mn) ? xn : mn;
    u64 xv[BN_MAX_LIMBS], mv[BN_MAX_LIMBS], shifted[BN_MAX_LIMBS];
    crypto_bn_from_be(xv, nn, x, x_len);
    crypto_bn_from_be(mv, nn, m, m_len);
    int xbits = crypto_bn_bitlen(xv, nn);
    int mbits = crypto_bn_bitlen(mv, nn);
    for (int i = xbits - mbits; i >= 0; i--) {
        crypto_bn_zero(shifted, nn);
        int limb_shift = i / 64, bit_shift = i % 64;
        for (int j = 0; j < mn; j++) {
            u64 lo = mv[j] << bit_shift;
            u64 hi = bit_shift ? (mv[j] >> (64 - bit_shift)) : 0;
            int dst = j + limb_shift;
            if (dst < nn) shifted[dst] |= lo;
            if (hi && dst + 1 < nn) shifted[dst + 1] |= hi;
        }
        if (crypto_bn_cmp(xv, shifted, nn) >= 0)
            (void)crypto_bn_sub(xv, xv, shifted, nn);
    }
    int on = (out_len + 7) / 8;
    u8 tmp[BN_MAX_BYTES];
    crypto_bn_to_be(xv, on, tmp, on * 8);
    if (out_len > (int)sizeof(tmp)) return -1;
    memcpy(out, tmp + (on * 8 - out_len), out_len);
    return 0;
}
