/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_util.h
 * Purpose: Freestanding string/memory helpers for OCIRE.
 */

#ifndef OCIRE_UTIL_H
#define OCIRE_UTIL_H

#include "ocire_types.h"

void  oc_memset(void *dst, u8 val, u64 len);
void *oc_memcpy(void *dst, const void *src, u64 len);
int   oc_memcmp(const void *a, const void *b, u64 len);
u64   oc_strlen(const char *s);
int   oc_strcmp(const char *a, const char *b);

/* Search a memory blob for a byte pattern; returns the offset or
 * (u64)-1 when not found.  Used by the ACPI \_S5 package scan. */
u64   oc_memfind(const void *haystack, u64 hay_len,
                 const void *needle, u64 needle_len);

/* Halt forever with interrupts off (final fallback path). */
void  oc_halt(void);

#endif /* OCIRE_UTIL_H */
