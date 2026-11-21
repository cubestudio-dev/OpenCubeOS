/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocbl/efi/ocbl_font.h
 * Purpose: the glyph table contract for the raw-flavor framebuffer
 *          console (chars 32..126, 8x16, bit 7 = leftmost pixel).
 */
#ifndef OCBL_FONT_H
#define OCBL_FONT_H

#include "../common/ocbl_types.h"

extern const u8 ocbl_font8x16[95][16];

#endif /* OCBL_FONT_H */
