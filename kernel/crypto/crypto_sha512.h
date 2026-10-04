/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/sha512.h
 * Purpose: SHA-512 and SHA-384 (FIPS 180-4), incremental API.
 *   Needed by: TLS 1.3 SHA-384 cipher suites, HMAC-SHA-512, SSH
 *   rsa-sha2-512 host keys, Ed25519 (SHA-512).
 */
#ifndef OC_SHA512_H
#define OC_SHA512_H

#include "types.h"

typedef struct {
    u64 h[8];
    u64 len_hi, len_lo;   /* total bytes */
    u8  buf[128];
    int buf_len;
} crypto_sha512_ctx_t;

void crypto_sha512_init(crypto_sha512_ctx_t *c);
void crypto_sha512_update(crypto_sha512_ctx_t *c, const u8 *data, int len);
void crypto_sha512_final(crypto_sha512_ctx_t *c, u8 out[64]);

void crypto_sha384_init(crypto_sha512_ctx_t *c);
/* update shared with sha512 */
void crypto_sha384_final(crypto_sha512_ctx_t *c, u8 out[48]);

/* one-shot */
void sha512(const u8 *data, int len, u8 out[64]);
void sha384(const u8 *data, int len, u8 out[48]);

/* HMAC-SHA-512 / HMAC-SHA-384 (RFC 2104) */
void crypto_hmac_sha512(const u8 *key, int key_len, const u8 *data, int data_len,
                 u8 hmac[64]);
void crypto_hmac_sha384(const u8 *key, int key_len, const u8 *data, int data_len,
                 u8 hmac[48]);

#endif /* OC_SHA512_H */
