/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03
 * File: services/font/font_engine.h
 * Purpose: the font-engine registry of the font root service.
 *
 * Formerly section 3 of the l1_ext.h grab-bag (services/l1/), split into
 * its owning root service in the WP-03 L0 restructure.  A font engine
 * produces glyph bitmaps on demand; the default engine serves glyphs
 * from the built-in screen_font8x16[] table, the CJK engines live next
 * to it in services/font/ (chinese_font + render_cn).
 *
 * The engine is registered by kmain at boot and by the display service
 * self-tests; callers look engines up by name.
 */
#ifndef OC_FONT_ENGINE_H
#define OC_FONT_ENGINE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A font engine: returns glyph_w * glyph_h / 8 bytes of bitmap data for
 * code point cp (each row is (glyph_w+7)/8 bytes). */
typedef struct screen_font_engine {
    const char* name;
    const u8* (*lookup)(struct screen_font_engine* self, u32 cp);
    u32 glyph_w;
    u32 glyph_h;
    /* Optional: list of code points the engine claims to support. NULL = all. */
} screen_font_engine_t;

int l1_ext_register_font_engine(screen_font_engine_t* engine);
screen_font_engine_t* l1_ext_font_engine_by_name(const char* name);
screen_font_engine_t* l1_ext_default_font_engine(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_FONT_ENGINE_H */
