/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_font.h
 * Purpose: 8x16 bitmap font used by the OCIRE LFB console
 *          (see ocire_font.c for provenance).
 */

#ifndef OCIRE_FONT_HEADER_GUARD
#define OCIRE_FONT_HEADER_GUARD

#include "ocire_types.h"

#define OCIRE_FONT_WIDTH  8
#define OCIRE_FONT_HEIGHT 16

extern const u8 ocire_font8x16[256][16];

#endif /* OCIRE_FONT_HEADER_GUARD */
