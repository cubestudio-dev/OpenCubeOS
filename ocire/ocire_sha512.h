/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_sha512.h
 * Purpose: SHA-512 (FIPS 180-4) for the OCIRE independent recovery
 *          kernel - the Ed25519 verify hash.
 *
 *          This is an independent OCIRE-side implementation (WP-01
 *          rule: OCIRE shares no code with the OCOS kernel).  The
 *          algorithm is standard SHA-512; only the incremental
 *          interface Ed25519 needs is exposed.
 */
#ifndef OCIRE_SHA512_H
#define OCIRE_SHA512_H

#include "ocire_types.h"

typedef struct {
    u64 h[8];
    u64 len_hi, len_lo;   /* total bytes */
    u8  buf[128];
    int buf_len;
} ocire_sha512_ctx_t;

void ocire_sha512_init(ocire_sha512_ctx_t *c);
void ocire_sha512_update(ocire_sha512_ctx_t *c, const u8 *data, int len);
void ocire_sha512_final(ocire_sha512_ctx_t *c, u8 out[64]);

/* one-shot */
void ocire_sha512(const u8 *data, int len, u8 out[64]);

#endif /* OCIRE_SHA512_H */
