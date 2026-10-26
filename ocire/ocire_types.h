/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_types.h
 * Purpose: Base scalar types for the OCIRE independent recovery kernel.
 *          Self-contained: OCIRE shares no headers with the OCOS kernel.
 */

#ifndef OCIRE_TYPES_H
#define OCIRE_TYPES_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;
typedef signed long long s64;

typedef unsigned long long uintptr;
typedef long long       intptr;

#define NULL ((void *)0)

#endif /* OCIRE_TYPES_H */
