/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/crypto.h
 * Purpose: Cryptographic primitives for TLS 1.2 + SSH.
 *   - AES-128-CTR (FIPS-197)
 *   - SHA-256 (FIPS-180-4)
 *   - HMAC-SHA-256 (RFC 2104)
 *   - HKDF-Expand (RFC 5869)
 *   - DH big-integer modular exponentiation (1024-bit, RFC 2409 Oakley Group 1)
 *
 * WP-09 design note: switched from 2048-bit (group 14) to 1024-bit (group 1)
 * for performance. 1024-bit schoolbook modexp in QEMU: ~5-10s vs ~60s.
 * For non-production TLS/SSH in QEMU this is acceptable.
 */
#ifndef OC_CRYPTO_H
#define OC_CRYPTO_H

#include "types.h"

/* DH modulus size in bytes — 1024-bit = 128 bytes (Oakley Group 1) */
#define DH_BYTES 128

/* ---- AES-128 (FIPS-197) ---- */
void aes128_encrypt_block(const u8 key[16], const u8 plaintext[16], u8 ciphertext[16]);
void aes128_decrypt_block(const u8 key[16], const u8 ciphertext[16], u8 plaintext[16]);
void aes128_ctr_encrypt(const u8 key[16], const u8 nonce[16], const u8 *in, int in_len, u8 *out);

/* AES-128-CBC encrypt/decrypt (in_len must be multiple of 16).
 * iv is 16 bytes; output is in_len bytes. For TLS 1.2, iv is the explicit
 * per-record IV (random). For decryption, iv is the first 16 bytes of record. */
void aes128_cbc_encrypt(const u8 key[16], const u8 iv[16], const u8 *in, int in_len, u8 *out);
void aes128_cbc_decrypt(const u8 key[16], const u8 iv[16], const u8 *in, int in_len, u8 *out);

/* ---- SHA-256 (FIPS-180-4) ---- */
void sha256(const u8 *data, int len, u8 hash[32]);

/* Streaming SHA-256: no large scratch buffers; supports inputs > 4 KB
 * (needed for TLS 1.3 transcript hashes over full certificate chains). */
typedef struct {
    u32 h[8];
    u64 total;      /* bytes processed */
    u8  buf[64];
    int buflen;
} sha256_ctx;
void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, int len);
void sha256_final(sha256_ctx *c, u8 hash[32]);

/* ---- HMAC-SHA-256 (RFC 2104) ---- */
void hmac_sha256(const u8 *key, int key_len, const u8 *data, int data_len, u8 hmac[32]);

/* ---- HKDF-Expand (RFC 5869) ---- */
void hkdf_expand(const u8 *prk, int prk_len, const u8 *info, int info_len, u8 *out, int out_len);

/* ---- TLS 1.3 key schedule (RFC 8446 section 7.1) ---- */
void tls13_ks_expand_label(const u8 *secret, int slen, const char *label,
                           const u8 *context, int ctx_len, u8 *out, int out_len);
void tls13_ks_derive_secret(const u8 *secret, const char *label,
                            const u8 *thash, int thash_len, u8 out[32]);

/* ---- DH big-integer (1024-bit, RFC 2409 Oakley Group 1) ---- */
/* Modular exponentiation: result = base^exp mod mod, all DH_BYTES-byte (1024-bit). */
void dh_modexp(const u8 base[DH_BYTES], const u8 exp[DH_BYTES],
               const u8 mod[DH_BYTES], u8 result[DH_BYTES]);

/* Generic modular exponentiation with explicit length (in bytes).
 * Used by SSH for 2048-bit (256-byte) group 14 DH. */
void dh_modexp_n(const u8 *base, const u8 *exp, const u8 *mod, u8 *result, int len);

/* Generate random bytes (using timer + xorshift PRNG — not cryptographic, but
 * sufficient for non-production TLS/SSH in QEMU). */
void crypto_random(u8 *buf, int len);

/* DH group 1 prime (1024-bit, RFC 2409 Oakley Group 1) */
extern const u8 dh_group1_prime[DH_BYTES];
extern const u8 dh_group14_prime[256];  /* RFC 3526 group 14 (2048-bit) */
extern const u8 dh_group1_generator[1]; /* g = 2 */

#endif /* OC_CRYPTO_H */
