/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/string.h
 * Purpose: Freestanding memory/string helpers (no libc).
 */
#ifndef OC_STRING_H
#define OC_STRING_H

#include "types.h"

void*  oc_memset (void* dst, int c, usize n);
void*  oc_memcpy (void* dst, const void* src, usize n);
void*  oc_memmove(void* dst, const void* src, usize n);
int    oc_memcmp(const void* a, const void* b, usize n);
usize  oc_strlen(const char* s);
int    oc_strcmp(const char* a, const char* b);
int    oc_strncmp(const char* a, const char* b, usize n);
char*  oc_strcpy(char* dst, const char* src);
char*  oc_strncpy(char* dst, const char* src, usize n);

/* WP-06: strcat/strchr helpers for network string building. */
char*  oc_strcat(char* dst, const char* src);
char*  oc_strchr(const char* s, char c);

/* WP-07: case-insensitive string compare (for FAT32 file name matching). */
int    oc_strcasecmp(const char* a, const char* b);

/* Decimal/hex formatters used by the boot log (no printf dependency). */
usize oc_u64_to_str(u64 v, char* out);                /* base 10, no leading zeros */
usize oc_u64_to_hex(u64 v, char* out, int min_digits);/* base 16, lowercase, 0-padded */

#endif /* OC_STRING_H */
