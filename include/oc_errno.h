/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-compat-and-security-hardening
 * File: include/oc_errno.h
 * Purpose: the unified L0 error-code vocabulary.
 *
 * Kernel/driver paths that must FAIL with a machine-readable code
 * return the NEGATED value (-OC_EPERM), mirroring the POSIX errno
 * convention the L1 apps already render.  Ring-3 surfaces (L1SVC
 * bodies, shell commands) map these to their user-facing text.
 *
 * Values follow the POSIX errno numbering so tooling (and muscle
 * memory) transfers 1:1; only the codes actually produced by L0 are
 * declared here - no speculative zoo. */
#ifndef OC_ERRNO_H
#define OC_ERRNO_H

/* Operation not permitted - the target is a protected resource
 * (e.g. a write against an OCIRE-armed recovery disk: the block
 * driver layer refuses it unconditionally). */
#define OC_EPERM   1

/* Read-only / write-protected media - the device or filesystem is
 * mounted or marked read-only. */
#define OC_EROFS  30

#endif /* OC_ERRNO_H */
