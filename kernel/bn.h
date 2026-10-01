/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/bn.h
 * Purpose: Generic big-integer arithmetic with Montgomery multiplication.
 *
 * This is the shared arithmetic core for:
 *   - RSA signature verification (PKCS#1 v1.5 + PSS), 2048..4096-bit
 *   - NIST P-256 / P-384 field and group arithmetic (ECDSA / ECDH)
 *
 * Numbers are little-endian arrays of u64 limbs with an explicit limb count
 * `n`. Max supported size is BN_MAX_LIMBS limbs (4608 bits), which covers
 * RSA-4096 with room to spare.
 *
 * Montgomery domain: for odd modulus m < 2^(64n), R = 2^(64n).
 *   mont_mul(a, b) = a*b*R^-1 mod m
 *   in : x  -> x*R mod m via mont_mul(x, RR)   (RR = R^2 mod m)
 *   out: xR -> x    mod m via mont_mul(xR, 1)
 */
#ifndef OC_BN_H
#define OC_BN_H

#include "types.h"

#define BN_MAX_LIMBS 72            /* 72 * 64 = 4608 bits */
#define BN_MAX_BYTES (BN_MAX_LIMBS * 8)

/* ---- Basic helpers (n = limb count, all arrays n limbs) ---- */
void bn_zero(u64 *r, int n);
void bn_copy(u64 *r, const u64 *a, int n);
/* Load from big-endian bytes (len <= 8*n; value < m assumed by callers). */
void bn_from_be(u64 *r, int n, const u8 *in, int len);
/* Store n-limb value to exactly out_len big-endian bytes (left-padded 0). */
void bn_to_be(const u64 *a, int n, u8 *out, int out_len);
/* Compare: returns <0, 0, >0 like memcmp in magnitude order. */
int  bn_cmp(const u64 *a, const u64 *b, int n);
int  bn_is_zero(const u64 *a, int n);
/* Bit length (0 when zero). */
int  bn_bitlen(const u64 *a, int n);

/* r = a + b, returns carry (1 if result overflows n limbs). */
u64  bn_add(u64 *r, const u64 *a, const u64 *b, int n);
/* r = a - b, returns borrow (1 if a < b). */
u64  bn_sub(u64 *r, const u64 *a, const u64 *b, int n);

/* r (2n limbs) = a * b (n limbs each). r must not alias a or b. */
void bn_mul(u64 *r, const u64 *a, const u64 *b, int n);

/* Montgomery REDC: for T < m*R (T is 2n limbs), compute T*R^-1 mod m.
 * Branchless final subtraction. out must not alias t. */
void bn_mont_redc(u64 *out, const u64 *t, const u64 *m, int n, u64 n0inv);

/* Montgomery multiply: out = a*b*R^-1 mod m (a, b < m). */
void bn_mont_mul(u64 *out, const u64 *a, const u64 *b,
                 const u64 *m, int n, u64 n0inv);

/* Compute n0inv = -m^-1 mod 2^64 and RR = R^2 mod m from odd modulus m. */
u64  bn_mont_n0inv(const u64 *m, int n);
void bn_mont_rr(u64 *rr, const u64 *m, int n);

/* Modular exponentiation (general purpose, odd modulus).
 *   out = base^exp mod m
 * base is big-endian bytes base_len long and MUST be < m;
 * exp  is big-endian bytes exp_len long;
 * mod  is big-endian bytes mod_len long (odd, <= BN_MAX_BYTES).
 * Uses the shared static workspace (NOT reentrant). Returns 0 on success,
 * -1 on size/parity error. */
int  bn_mod_exp(u8 *out, int out_len,
                const u8 *base, int base_len,
                const u8 *exp, int exp_len,
                const u8 *mod, int mod_len);

/* x mod m for arbitrary (even) m — slow bit-shift-subtract, only used for
 * tiny reductions (ECDSA scalar bounds); x is big-endian, len <= BN_MAX_BYTES.
 * Returns 0 on success. */
int  bn_mod_bytes(u8 *out, int out_len, const u8 *x, int x_len,
                  const u8 *m, int m_len);

#endif /* OC_BN_H */
