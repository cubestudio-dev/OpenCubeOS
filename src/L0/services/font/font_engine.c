/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03
 * File: services/font/font_engine.c
 * Purpose: font-engine registry (moved verbatim from services/l1/l1_ext.c
 * in the WP-03 L0 restructure; the l1_ext grab-bag was split into its
 * owning root services).  Default wiring: the default engine wraps the
 * built-in screen_font8x16[] table.
 */
#include "font_engine.h"
#include "screen_font.h"
#include "lib_string.h"

#define OC_MAX_FONT_ENGINES 8
static screen_font_engine_t* g_engines[OC_MAX_FONT_ENGINES];
static int g_engine_count = 0;

/* Default font engine: serves glyphs from the built-in screen_font8x16 table. */
static const u8* default_font_lookup(screen_font_engine_t* self, u32 cp) {
    (void)self;
    return screen_font8x16[cp & 0xFF];
}
static screen_font_engine_t g_default_engine = {
    .name     = "default-8x16-vga",
    .lookup   = default_font_lookup,
    .glyph_w  = OC_FONT_CELL_W,
    .glyph_h  = OC_FONT_CELL_H,
};

int l1_ext_register_font_engine(screen_font_engine_t* engine) {
    if (!engine || !engine->name || !engine->lookup) return -1;
    if (g_engine_count >= OC_MAX_FONT_ENGINES) return -2;
    /* De-dup: if name matches, replace. */
    for (int i = 0; i < g_engine_count; ++i) {
        if (g_engines[i] && strcmp(g_engines[i]->name, engine->name) == 0) {
            g_engines[i] = engine;
            return 0;
        }
    }
    g_engines[g_engine_count++] = engine;
    return 0;
}

screen_font_engine_t* l1_ext_font_engine_by_name(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_engine_count; ++i) {
        if (g_engines[i] && strcmp(g_engines[i]->name, name) == 0)
            return g_engines[i];
    }
    return NULL;
}

screen_font_engine_t* l1_ext_default_font_engine(void) {
    return &g_default_engine;
}

/* Note: the default font engine is registered explicitly by kmain at boot.
 * (Freestanding kernels don't run __attribute__((constructor)) ctors.) */
