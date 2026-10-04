/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/rsa.h
 * Purpose: RSASSA signature verification (RFC 8017).
 *   - PKCS#1 v1.5 (X.509 certificate chains, TLS 1.2 ECDHE_RSA)
 *   - PSS with salt length == hash length (TLS 1.3 CertificateVerify)
 * SHA-256 / SHA-384 / SHA-512.
 */
#ifndef OC_RSA_H
#define OC_RSA_H

#include "types.h"

/* Digest selectors */
#define RSA_SHA256 1
#define RSA_SHA384 2
#define RSA_SHA512 3

/* Verify RSASSA-PKCS1-v1_5 over a digest.
 * n: modulus big-endian (n_len bytes); e: exponent big-endian;
 * sig: signature, sig_len == n_len; hash: digest bytes.
 * Returns 1 = valid, 0 = invalid, -1 = parameter error. */
int crypto_rsa_verify_pkcs1(const u8 *n, int n_len, const u8 *e, int e_len,
                     int sha_alg, const u8 *hash, int hash_len,
                     const u8 *sig, int sig_len);

/* Verify RSASSA-PSS (salt length == hash length, MGF1 with same hash).
 * Returns 1 = valid, 0 = invalid, -1 = parameter error. */
int crypto_rsa_verify_pss(const u8 *n, int n_len, const u8 *e, int e_len,
                   int sha_alg, const u8 *hash, int hash_len,
                   const u8 *sig, int sig_len);

#endif /* OC_RSA_H */
