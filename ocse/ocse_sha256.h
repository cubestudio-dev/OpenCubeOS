/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_sha256.h
 * Purpose: streaming SHA-256 for the installer CLI (sha256sum, payload
 *   integrity cross-checks, later the ocbl-install verification).
 *
 * Ported verbatim (math + structure) from the v1.0.0 kernel crypto
 * core (kernel/crypto/crypto_core.c, FIPS-180-4) - the vendored 835-line
 * file carries DRBG/AES/TLS dependencies (crypto_irq_window, core_timer,
 * mem_pmm) that the OCSE subset does not ship, so only the zero-depend
 * SHA-256 section is carried over, renamed to the ocse_ prefix.
 * The ustar determinism rule (make_ocos_tar.py) plus this digest gives
 * the installer an end-to-end "what I read is what I wrote" proof.
 */
#ifndef OCSE_SHA256_H
#define OCSE_SHA256_H

#include "types.h"

typedef struct {
    u32 h[8];
    u64 total;                      /* bytes fed so far */
    u32 buflen;                     /* bytes parked in buf (< 64) */
    u8  buf[64];
} ocse_sha256_ctx;

void ocse_sha256_init(ocse_sha256_ctx *c);
void ocse_sha256_update(ocse_sha256_ctx *c, const void *data, int len);
void ocse_sha256_final(ocse_sha256_ctx *c, u8 hash[32]);

/* One-shot convenience. */
void ocse_sha256(const u8 *data, int len, u8 hash[32]);

/* hexhash: 64 lowercase hex chars + NUL into out[65]. */
void ocse_sha256_hex(const u8 hash[32], char out[65]);

#endif /* OCSE_SHA256_H */
