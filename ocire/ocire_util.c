/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_util.c
 * Purpose: Freestanding string/memory helpers for OCIRE.
 */

#include "ocire_util.h"
#include "ocire_io.h"

void oc_memset(void *dst, u8 val, u64 len) {
    u8 *d = (u8 *)dst;
    while (len--) *d++ = val;
}

void *oc_memcpy(void *dst, const void *src, u64 len) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (len--) *d++ = *s++;
    return dst;
}

int oc_memcmp(const void *a, const void *b, u64 len) {
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    for (u64 i = 0; i < len; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

u64 oc_strlen(const char *s) {
    u64 n = 0;
    while (s && s[n]) n++;
    return n;
}

int oc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

u64 oc_memfind(const void *haystack, u64 hay_len,
               const void *needle, u64 needle_len) {
    if (!haystack || !needle || needle_len == 0 || hay_len < needle_len)
        return (u64)-1;
    const u8 *h = (const u8 *)haystack;
    u64 last = hay_len - needle_len;
    for (u64 i = 0; i <= last; i++) {
        if (oc_memcmp(h + i, needle, needle_len) == 0) return i;
    }
    return (u64)-1;
}

void oc_halt(void) {
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
