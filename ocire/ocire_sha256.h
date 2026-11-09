/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_sha256.h
 * Purpose: SHA-256 (FIPS 180-4) for the OCIRE recovery kernel.
 *
 * Used by `syscheck` (system-file integrity against the golden image
 * manifest) and by the boot supervisor (agent-image digest written
 * into the mailbox so OCOS can refuse a tampered agent).
 *
 * Independent implementation: OCIRE shares no crypto source with the
 * OCOS kernel; this is the standard SHA-256 from the specification.
 */

#ifndef OCIRE_SHA256_H
#define OCIRE_SHA256_H

#include "ocire_types.h"

typedef struct {
    u32 h[8];
    u64 len;              /* total message bytes so far */
    u8  buf[64];
    u32 buflen;
} ocire_sha256_ctx;

void ocire_sha256_init(ocire_sha256_ctx *c);
void ocire_sha256_update(ocire_sha256_ctx *c, const void *data, u64 len);
/* Finalizes and writes the 32-byte digest.  The context is left
 * unusable (re-init for a new message). */
void ocire_sha256_final(ocire_sha256_ctx *c, u8 out[32]);

/* One-shot helper. */
void ocire_sha256(const void *data, u64 len, u8 out[32]);

/* Render a 32-byte digest as 64 lowercase hex chars (+NUL). */
void ocire_sha256_hex(const u8 digest[32], char out[65]);

/* Constant-time comparison of two digests: 0 = equal. */
int  ocire_sha256_eq(const u8 a[32], const u8 b[32]);

#endif /* OCIRE_SHA256_H */
