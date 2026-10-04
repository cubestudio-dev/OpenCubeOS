/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/aead.h
 * Purpose: AEAD ciphers for TLS 1.3 / TLS 1.2 / SSH.
 *   - AES-128/256-GCM (RFC 5116 / NIST SP 800-38D)
 *   - ChaCha20-Poly1305 (RFC 8439)
 *   - AES block primitives (encrypt-only core; GCM/CTR need no decrypt)
 *
 * All APIs take a 12-byte nonce. Tag is 16 bytes. Returns 0 on success,
 * -1 on auth failure / bad parameters. In-place operation (pt == ct) is
 * supported for decrypt.
 */
#ifndef OC_AEAD_H
#define OC_AEAD_H

#include "types.h"

/* ---- AES-GCM (key_bits: 128 or 256) ---- */
int crypto_aes128_gcm_seal(const u8 key[16], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *pt, int pt_len, u8 *ct, u8 tag[16]);
int crypto_aes128_gcm_open(const u8 key[16], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *ct, int ct_len, u8 *pt, const u8 tag[16]);
int crypto_aes256_gcm_seal(const u8 key[32], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *pt, int pt_len, u8 *ct, u8 tag[16]);
int crypto_aes256_gcm_open(const u8 key[32], const u8 nonce[12],
                    const u8 *aad, int aad_len,
                    const u8 *ct, int ct_len, u8 *pt, const u8 tag[16]);

/* ---- ChaCha20-Poly1305 (RFC 8439 AEAD construction) ---- */
int chacha20poly1305_seal(const u8 key[32], const u8 nonce[12],
                          const u8 *aad, int aad_len,
                          const u8 *pt, int pt_len, u8 *ct, u8 tag[16]);
int chacha20poly1305_open(const u8 key[32], const u8 nonce[12],
                          const u8 *aad, int aad_len,
                          const u8 *ct, int ct_len, u8 *pt, const u8 tag[16]);

/* ---- Raw building blocks (also used by SSH) ---- */
/* ChaCha20 stream: encrypt/decrypt len bytes (block counter starts at ctr). */
void crypto_chacha20_xor(const u8 key[32], const u8 nonce[12], u32 ctr,
                  const u8 *in, int len, u8 *out);
/* Poly1305 MAC over arbitrary data. */
void crypto_poly1305_mac(const u8 key[32], const u8 *data, int len, u8 tag[16]);

#endif /* OC_AEAD_H */
