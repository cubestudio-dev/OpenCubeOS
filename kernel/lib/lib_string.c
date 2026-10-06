/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/string.c
 */
#include "lib_string.h"

void* memset(void* dst, int c, usize n) {
    u8* d = (u8*)dst;
    u8 v = (u8)c;
    while (n--) *d++ = v;
    return dst;
}

void* memcpy(void* dst, const void* src, usize n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, usize n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, usize n) {
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        ++x; ++y;
    }
    return 0;
}

usize strlen(const char* s) {
    const char* p = s;
    while (*p) ++p;
    return (usize)(p - s);
}

int strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return (int)*a - (int)*b;
        ++a; ++b;
    }
    return (int)*a - (int)*b;
}

int strncmp(const char* a, const char* b, usize n) {
    while (n && *a && *b) {
        if (*a != *b) return (int)*a - (int)*b;
        ++a; ++b; --n;
    }
    if (!n) return 0;
    return (int)*a - (int)*b;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

/* BUG-0112 FIX: the old loop `while (n && (*d++ = *src++)) --n;`
 * consumed one extra byte of the budget when the source ran out: the
 * terminating NUL was stored by the copy loop itself and the padding
 * loop then ran with an un-decremented count, writing n+1 bytes total
 * (one past the contract). Callers that sized their buffer as exactly
 * `n` saw a one-byte stack/heap overwrite. This version stores the
 * terminator via the padding loop only and writes EXACTLY n bytes,
 * matching ISO C strncpy semantics. */
char* strncpy(char* dst, const char* src, usize n) {
    char* d = dst;
    while (n && *src) { *d++ = *src++; n--; }
    while (n--) *d++ = 0;
    return dst;
}

usize u64_to_str(u64 v, char* out) {
    char tmp[20];
    usize i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return 1; }
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    /* reverse */
    for (usize j = 0; j < i; ++j) out[j] = tmp[i - 1 - j];
    out[i] = 0;
    return i;
}

usize u64_to_hex(u64 v, char* out, int min_digits) {
    /* BUG-042 FIX: Cap min_digits at 16 to prevent tmp[16] overflow.
     * A u64 has at most 16 hex digits. If min_digits > 16, the old code
     * wrote past tmp[16]. Now clamped. */
    const char* hex = "0123456789abcdef";
    char tmp[16];
    usize i = 0;
    if (v == 0) { tmp[i++] = '0'; }
    while (v) { tmp[i++] = hex[v & 0xF]; v >>= 4; }
    while ((int)i < min_digits && i < 16) tmp[i++] = '0';
    /* reverse */
    usize n = i;
    for (usize j = 0; j < n; ++j) out[j] = tmp[n - 1 - j];
    out[n] = 0;
    return n;
}

/* WP-06: strcat - append src to dst. */
char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

/* WP-06: strchr - find first occurrence of c in s. */
char* strchr(const char* s, char c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : 0;
}

/* WP-07: strcasecmp - case-insensitive string comparison. */
int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
