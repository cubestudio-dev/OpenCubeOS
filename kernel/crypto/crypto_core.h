/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09
 * File: kernel/crypto.h
 * Purpose: Core cryptographic primitives for TLS 1.3 / TLS 1.2 / SSH.
 *   - AES-128 block / CTR / CBC (FIPS-197)
 *   - SHA-256 one-shot + streaming (FIPS-180-4)
 *   - HMAC-SHA-256 (RFC 2104)
 *   - HKDF-Expand (RFC 5869)
 *   - TLS 1.3 key schedule (RFC 8446 S7.1)
 *   - DH big-integer modular exponentiation (generic length via
 *     crypto_dh_modexp_n; fixed 1024-bit helper for the cryptotest self-test)
 *
 * Companion primitive modules live in their own headers:
 *   - aead.h      AES-GCM / ChaCha20-Poly1305 (TLS record protection)
 *   - sha512.h    SHA-384/512 (TLS 1.3 AES-256-GCM suite)
 *   - bn.h        big-number arithmetic (RSA, ECDSA)
 *   - rsa.h       RSA verification/signing (TLS certs, SSH host keys)
 *   - crypto_ec_nist.h   NIST P-256 ECDH/ECDSA (TLS 1.2 ECDHE, X.509)
 *   - curve25519.h X25519 (TLS 1.3, SSH curve25519-sha256)
 *   - x509.h      X.509 certificate parsing + CA chain validation (TLS)
 */
#ifndef OC_CRYPTO_H
#define OC_CRYPTO_H

#include "types.h"

/* DH modulus size in bytes — 1024-bit = 128 bytes (Oakley Group 1).
 * Used by the fixed-size crypto_dh_modexp() self-test path; SSH (group 14) and
 * TLS DHE use crypto_dh_modexp_n() with explicit length instead. */
#define DH_BYTES 128

/* ---- AES-128 (FIPS-197) ---- */
void crypto_aes128_encrypt_block(const u8 key[16], const u8 plaintext[16], u8 ciphertext[16]);
void crypto_aes128_decrypt_block(const u8 key[16], const u8 ciphertext[16], u8 plaintext[16]);
void crypto_aes128_ctr_encrypt(const u8 key[16], const u8 nonce[16], const u8 *in, int in_len, u8 *out);

/* AES-128-CBC encrypt/decrypt (in_len must be multiple of 16).
 * iv is 16 bytes; output is in_len bytes. For TLS 1.2, iv is the explicit
 * per-record IV (random). For decryption, iv is the first 16 bytes of record. */
void crypto_aes128_cbc_encrypt(const u8 key[16], const u8 iv[16], const u8 *in, int in_len, u8 *out);
void crypto_aes128_cbc_decrypt(const u8 key[16], const u8 iv[16], const u8 *in, int in_len, u8 *out);

/* ---- SHA-256 (FIPS-180-4) ---- */
void sha256(const u8 *data, int len, u8 hash[32]);

/* Streaming SHA-256: no large scratch buffers; supports inputs > 4 KB
 * (needed for TLS 1.3 transcript hashes over full certificate chains). */
typedef struct {
    u32 h[8];
    u64 total;      /* bytes processed */
    u8  buf[64];
    int buflen;
} crypto_sha256_ctx;
void crypto_sha256_init(crypto_sha256_ctx *c);
void crypto_sha256_update(crypto_sha256_ctx *c, const void *data, int len);
void crypto_sha256_final(crypto_sha256_ctx *c, u8 hash[32]);

/* ---- HMAC-SHA-256 (RFC 2104) ---- */
void crypto_hmac_sha256(const u8 *key, int key_len, const u8 *data, int data_len, u8 hmac[32]);

/* ---- HKDF-Expand (RFC 5869) ---- */
void hkdf_expand(const u8 *prk, int prk_len, const u8 *info, int info_len, u8 *out, int out_len);

/* ---- TLS 1.3 key schedule (RFC 8446 section 7.1) ---- */
void net_tls13_ks_expand_label(const u8 *secret, int slen, const char *label,
                           const u8 *context, int ctx_len, u8 *out, int out_len);
void net_tls13_ks_derive_secret(const u8 *secret, const char *label,
                            const u8 *thash, int thash_len, u8 out[32]);

/* ---- DH big-integer (fixed 1024-bit, RFC 2409 Oakley Group 1) ---- */
/* Modular exponentiation: result = base^exp mod mod, all DH_BYTES-byte (1024-bit).
 * Self-test variant (boot-time cryptotest); protocol code uses crypto_dh_modexp_n(). */
void crypto_dh_modexp(const u8 base[DH_BYTES], const u8 exp[DH_BYTES],
               const u8 mod[DH_BYTES], u8 result[DH_BYTES]);

/* Generic modular exponentiation with explicit length (in bytes).
 * Used by SSH for 2048-bit (256-byte) group 14 DH. */
void crypto_dh_modexp_n(const u8 *base, const u8 *exp, const u8 *mod, u8 *result, int len);

/* Generate random bytes (using timer + xorshift PRNG — not cryptographic, but
 * sufficient for non-production TLS/SSH in QEMU). */
void crypto_random(u8 *buf, int len);

/* DH group 1 prime (1024-bit, RFC 2409 Oakley Group 1) */
extern const u8 crypto_dh_group1_prime[DH_BYTES];
extern const u8 crypto_dh_group14_prime[256];  /* RFC 3526 group 14 (2048-bit) */
extern const u8 crypto_dh_group1_generator[1]; /* g = 2 */

#endif /* OC_CRYPTO_H */
