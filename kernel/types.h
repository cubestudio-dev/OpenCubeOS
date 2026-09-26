/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/types.h
 * Purpose: Freestanding primitive types + helper macros.
 *          No libc available in the kernel; we define the bare minimum.
 */
#ifndef OC_TYPES_H
#define OC_TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef short               i16;
typedef int                 i32;
typedef long long           i64;
typedef unsigned long       usize;
typedef long                isize;

typedef unsigned long       uintptr_t;
typedef long                intptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define true 1
#define false 0
typedef u8 bool;

#define OC_API          /* exported kernel symbol */
#define OC_EXT_API      /* exported extension API symbol */
#define OC_WEAK         __attribute__((weak))

#define OC_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define oc_container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof_container(ptr, type, member)))

static inline uintptr_t oc_align_up(uintptr_t v, uintptr_t a) {
    return (v + (a - 1)) & ~(a - 1);
}
static inline uintptr_t oc_align_down(uintptr_t v, uintptr_t a) {
    return v & ~(a - 1);
}

/* offsetof - provided by GCC __builtin_offsetof, but we wrap for clarity */
#define offsetof_container(p, t, m) __builtin_offsetof(t, m)

#endif /* OC_TYPES_H */
