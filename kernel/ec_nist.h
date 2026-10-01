/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/ec_nist.h
 * Purpose: NIST prime curves P-256 and P-384 (FIPS 186-4).
 *   - ECDH (TLS 1.2 ECDHE suites, SSH ecdh-sha2-nistp256/384)
 *   - ECDSA signature verification (X.509 ECDSA certs, SSH host keys)
 *
 * Coordinates live in the Montgomery domain of the curve prime; the affine
 * API below takes/returns plain big-endian bytes. All arithmetic sits on
 * top of bn.c (generic Montgomery core). Verification code only — no
 * secret-dependent branches in the public-key paths used here (ECDSA verify
 * and ECDH with our own freshly generated scalars).
 */
#ifndef OC_EC_NIST_H
#define OC_EC_NIST_H

#include "types.h"

/* Curve ids */
#define EC_P256 0
#define EC_P384 1

/* Byte sizes */
#define EC_P256_SIZE 32
#define EC_P384_SIZE 48
#define EC_MAX_SCALAR 48

/* ---- ECDH ---- */

/* Public point Q = priv*G (priv: field-size big-endian scalar).
 * pub_out: UNCOMPRESSED point (0x04 || X || Y), 33/49 bytes.
 * Returns 0 on success. */
int ec_pub_from_priv(int curve, const u8 *priv, int priv_len,
                     u8 *pub_out, int pub_len);

/* Generate a private scalar and the matching public point.
 * pub_out: UNCOMPRESSED point (0x04 || X || Y), 33/49 bytes.
 * Returns 0 on success. */
int ec_keygen(int curve, u8 *priv_out, int priv_len, u8 *pub_out, int pub_len);

/* Shared secret: X coordinate of priv * peer_pub.
 * shared_out: field-size big-endian bytes.
 * Returns 0 on success, -1 on invalid input (bad point / infinity). */
int ec_ecdh(int curve, const u8 *priv, int priv_len,
            const u8 *peer_pub, int pub_len, u8 *shared_out, int shared_len);

/* Validate a public point (on curve, not infinity). 0 = valid. */
int ec_pub_valid(int curve, const u8 *pub, int pub_len);

/* ---- ECDSA verify ---- */

/* Verify (r, s) over hash with public key.
 * hash: raw digest bytes (32 for SHA-256, 48 for SHA-384, 64 for SHA-512 —
 *       leftmost bits are used per FIPS 186-4 when hash is longer than n).
 * r/s: field-size big-endian. pub: uncompressed point.
 * Returns 1 = valid, 0 = invalid, -1 = parse/size error. */
int ecdsa_verify(int curve, const u8 *pub, int pub_len,
                 const u8 *hash, int hash_len,
                 const u8 *r, int r_len, const u8 *s, int s_len);

#endif /* OC_EC_NIST_H */
