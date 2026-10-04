/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/string.h
 * Purpose: Freestanding memory/string helpers (no libc).
 */
#ifndef OC_STRING_H
#define OC_STRING_H

#include "types.h"

void*  memset (void* dst, int c, usize n);
void*  memcpy (void* dst, const void* src, usize n);
void*  memmove(void* dst, const void* src, usize n);
int    memcmp(const void* a, const void* b, usize n);
usize  strlen(const char* s);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, usize n);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, usize n);

/* WP-06: strcat/strchr helpers for network string building. */
char*  strcat(char* dst, const char* src);
char*  strchr(const char* s, char c);

/* WP-07: case-insensitive string compare (for FAT32 file name matching). */
int    strcasecmp(const char* a, const char* b);

/* Decimal/hex formatters used by the boot log (no printf dependency). */
usize u64_to_str(u64 v, char* out);                /* base 10, no leading zeros */
usize u64_to_hex(u64 v, char* out, int min_digits);/* base 16, lowercase, 0-padded */

#endif /* OC_STRING_H */
