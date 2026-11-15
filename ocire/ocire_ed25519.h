/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_ed25519.h
 * Purpose: Ed25519 signature VERIFICATION (RFC 8032) for the OCIRE
 *          secure OTA path.
 *
 *          Why Ed25519 here: the WP-07 spec mandates built-in public
 *          key verification of `ocire-update.bin` before any flash.
 *          Ed25519 gives 128-bit security with a 32-byte public key
 *          and a 64-byte signature - the smallest mainstream choice,
 *          and the same field arithmetic the OCOS crypto service
 *          already ships for X25519 (this is an INDEPENDENT OCIRE
 *          implementation per the WP-01 rule, no shared code).
 *
 *          Verification equation (RFC 8032 section 5.1.7, the
 *          mainstream cofactorless form used by libsodium's default
 *          and OpenSSL):
 *              k  = SHA-512(R || A || M)  interpreted little-endian
 *              check  [S]B == R + [k]A
 *          Inputs that fail point decompression, carry S >= L, or do
 *          not satisfy the equation are rejected with -1.  The
 *          verification is NOT constant time (public data only).
 *
 *          The trusted public key lives in the generated source
 *          ocire/generated/ocire_ed25519_pub.c (see
 *          tools/gen_ocire_ed25519.py - the private key never
 *          enters the repository).
 */
#ifndef OCIRE_ED25519_H
#define OCIRE_ED25519_H

#include "ocire_types.h"

/* The built-in trust anchor (32 bytes, little-endian as standard). */
extern const u8 ocire_ed25519_pubkey[32];

/* Verify a 64-byte Ed25519 signature over `len` bytes at `msg`,
 * against the built-in public key.  Returns 0 on success, negative
 * on any failure. */
int ocire_ed25519_verify(const u8 *msg, u64 len, const u8 sig[64]);

/* Verify against an EXPLICIT public key (test vectors + future
 * key rotation).  Same return contract. */
int ocire_ed25519_verify_pk(const u8 pk[32], const u8 *msg, u64 len,
                            const u8 sig[64]);

#endif /* OCIRE_ED25519_H */
