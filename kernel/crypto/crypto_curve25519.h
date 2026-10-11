/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/curve25519.h
 * Purpose: X25519 Diffie-Hellman (RFC 7748).
 *   Used by TLS 1.3 key_share, TLS 1.2 ECDHE, SSH curve25519-sha256 kex.
 *   Constant-time Montgomery ladder (secret scalars are involved).
 */
#ifndef OC_CURVE25519_H
#define OC_CURVE25519_H

#include "types.h"

#define X25519_KEY_SIZE 32

/* out = scalar * point (32-byte each). Returns 0 on success.
 * Raw primitive: the all-zero check is intentionally NOT done here (the
 * base-point public-key path can never produce it).
 * BUG-0296: the shared-secret wrapper below enforces the RFC 8446 7.4.3
 * all-zero rejection. */
int x25519(u8 out[32], const u8 scalar[32], const u8 point[32]);

/* Generate a public key from a (random) private scalar:
 * applies the RFC 7748 scalar clamping. */
int crypto_x25519_public(const u8 priv[32], u8 pub[32]);

/* Shared secret helper: applies clamping to priv, then x25519.
 * BUG-0296 (A4-09) / RFC 8446 section 7.4.3: returns -1 when the peer public
 * key has small order and the shared secret collapses to all zeros. */
int crypto_x25519_shared(const u8 priv[32], const u8 peer_pub[32], u8 shared[32]);

#endif /* OC_CURVE25519_H */
