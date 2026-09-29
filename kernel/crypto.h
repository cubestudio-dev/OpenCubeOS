/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/crypto.h
 * Purpose: Cryptographic primitives for TLS 1.2 + SSH.
 *   - AES-128-CTR (FIPS-197)
 *   - SHA-256 (FIPS-180-4)
 *   - HMAC-SHA-256 (RFC 2104)
 *   - DH big-integer modular exponentiation (2048-bit)
 */
#ifndef OC_CRYPTO_H
#define OC_CRYPTO_H

#include "types.h"

/* ---- AES-128 (FIPS-197) ---- */
void aes128_encrypt_block(const u8 key[16], const u8 plaintext[16], u8 ciphertext[16]);
void aes128_ctr_encrypt(const u8 key[16], const u8 nonce[16], const u8 *in, int in_len, u8 *out);

/* ---- SHA-256 (FIPS-180-4) ---- */
void sha256(const u8 *data, int len, u8 hash[32]);

/* ---- HMAC-SHA-256 (RFC 2104) ---- */
void hmac_sha256(const u8 *key, int key_len, const u8 *data, int data_len, u8 hmac[32]);

/* ---- HKDF-Expand (RFC 5869) ---- */
void hkdf_expand(const u8 *prk, int prk_len, const u8 *info, int info_len, u8 *out, int out_len);

/* ---- DH big-integer (2048-bit, RFC 3526 group 14) ---- */
/* Modular exponentiation: result = base^exp mod mod, all 256-byte (2048-bit) */
void dh_modexp(const u8 base[256], const u8 exp[256], const u8 mod[256], u8 result[256]);

/* Generate random bytes (using timer + counter as PRNG — not cryptographic, but
 * sufficient for non-production TLS/SSH in QEMU). */
void crypto_random(u8 *buf, int len);

/* DH group 14 prime (2048-bit, RFC 3526) */
extern const u8 dh_group14_prime[256];
extern const u8 dh_group14_generator[1]; /* g = 2 */

#endif /* OC_CRYPTO_H */
