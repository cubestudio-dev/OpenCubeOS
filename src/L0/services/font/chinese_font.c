/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/chinese_font.c
 * Purpose: GB2312 16x16 bitmap font - management and lookup.
 *
 * The glyph data lives in chinese_font_data.c (generated). This file
 * only implements the slot registry and the four lookup directions:
 *   zone/wei -> index, unicode -> index, index -> bitmap,
 *   index -> unicode.
 */
#include "chinese_font.h"
#include "lib_string.h"
#include "core_sync.h"

/* Slot table. Slot 0 is the embedded font and is always resident. */
typedef struct chinese_font_slot {
    const char* name;
    const u8*   hanzi;         /* hanzi_count * 32 bytes               */
    const u8*   symbols;       /* symbol_count * 32 bytes (may be NULL) */
    const u16*  uto_gb;        /* flat {unicode, idx} pairs, sorted    */
    u32         hanzi_count;
    u32         symbol_count;
    u32         uto_count;     /* number of {unicode, idx} pairs       */
    int         in_use;
} chinese_font_slot_t;

static chinese_font_slot_t g_slots[CHINESE_FONT_MAX_SLOTS];
static spinlock_t g_font_lock;
static int g_font_ready;
static u32 g_lookups;
static u32 g_misses;

/* Generated data (chinese_font_data.c). */
extern const u8  chinese_font_glyphs[CHINESE_FONT_HANZI_COUNT][CHINESE_FONT_GLYPH_BYTES];
extern const u8  chinese_font_symbols[CHINESE_FONT_SYMBOL_COUNT][CHINESE_FONT_GLYPH_BYTES];
extern const u16 chinese_font_zone_index[6768];
extern const u16 chinese_font_symbol_index[846];
extern const u16 chinese_font_uto_gb[2 * CHINESE_FONT_UTO_COUNT];
extern const u16 chinese_font_idx_to_unicode[CHINESE_FONT_UTO_COUNT];

static const char* k_embedded_name = "embedded-unifont-16x16";

int chinese_font_init(void) {
    if (g_font_ready) return 0;
    spin_init(&g_font_lock);
    spin_lock(&g_font_lock);
    for (u32 i = 0; i < CHINESE_FONT_MAX_SLOTS; ++i) {
        g_slots[i].name = NULL;
        g_slots[i].in_use = 0;
    }
    g_slots[0].name         = k_embedded_name;
    g_slots[0].hanzi        = &chinese_font_glyphs[0][0];
    g_slots[0].symbols      = &chinese_font_symbols[0][0];
    g_slots[0].uto_gb       = chinese_font_uto_gb;
    g_slots[0].hanzi_count  = CHINESE_FONT_HANZI_COUNT;
    g_slots[0].symbol_count = CHINESE_FONT_SYMBOL_COUNT;
    g_slots[0].uto_count    = CHINESE_FONT_UTO_COUNT;
    g_slots[0].in_use       = 1;
    g_lookups = 0;
    g_misses  = 0;
    spin_unlock(&g_font_lock);
    g_font_ready = 1;
    return 0;
}

int chinese_font_shutdown(void) {
    if (!g_font_ready) return 0;
    spin_lock(&g_font_lock);
    for (u32 i = 1; i < CHINESE_FONT_MAX_SLOTS; ++i) {
        g_slots[i].in_use = 0;
        g_slots[i].name = NULL;
    }
    g_lookups = 0;
    g_misses  = 0;
    spin_unlock(&g_font_lock);
    return 0;
}

int chinese_font_load(void) {
    if (!g_font_ready) chinese_font_init();
    if (g_slots[0].in_use) return 0;
    /* Slot 0 is const data - it can only be missing before init. */
    return chinese_font_init();
}

int chinese_font_load_mem(const char* name,
                          const u8* hanzi, u32 hanzi_count,
                          const u8* symbols, u32 symbol_count,
                          const u16* uto_gb, u32 uto_count) {
    if (!g_font_ready) chinese_font_init();
    if (!name || !hanzi || hanzi_count == 0 || !uto_gb || uto_count == 0) {
        return -2;
    }
    if (hanzi_count > 0xFFFF || symbol_count > 0x7FFF || uto_count > 0xFFFF) {
        return -2;   /* compact indexes are u16 */
    }
    spin_lock(&g_font_lock);
    int slot = -1;
    for (u32 i = 1; i < CHINESE_FONT_MAX_SLOTS; ++i) {
        if (!g_slots[i].in_use) { slot = (int)i; break; }
    }
    if (slot < 0) {
        spin_unlock(&g_font_lock);
        return -1;
    }
    chinese_font_slot_t* s = &g_slots[slot];
    s->name         = name;
    s->hanzi        = hanzi;
    s->symbols      = symbols;   /* NULL is allowed: hanzi-only runtime font */
    s->uto_gb       = uto_gb;
    s->hanzi_count  = hanzi_count;
    s->symbol_count = symbols ? symbol_count : 0;
    s->uto_count    = uto_count;
    s->in_use       = 1;
    spin_unlock(&g_font_lock);
    return slot;
}

int chinese_font_unload(chinese_font_handle_t slot) {
    if (slot == 0) return -5;               /* embedded font is permanent */
    if (slot < 0 || (u32)slot >= CHINESE_FONT_MAX_SLOTS) return -3;
    spin_lock(&g_font_lock);
    g_slots[slot].in_use = 0;
    g_slots[slot].name   = NULL;
    spin_unlock(&g_font_lock);
    return 0;
}

static const chinese_font_slot_t* slot_ok(int slot) {
    if (!g_font_ready || slot < 0 || (u32)slot >= CHINESE_FONT_MAX_SLOTS) {
        return NULL;
    }
    return g_slots[slot].in_use ? &g_slots[slot] : NULL;
}

int chinese_font_lookup(chinese_font_handle_t slot, u32 zone, u32 wei) {
    const chinese_font_slot_t* s = slot_ok(slot);
    if (!s) return -3;
    g_lookups++;
    if (wei == 0 || wei > 94) { g_misses++; return -1; }
    if (zone >= 16 && zone <= 87 && s->hanzi_count > 0) {
        u16 idx = chinese_font_zone_index[(zone - 16) * 94 + (wei - 1)];
        if (idx != 0xFFFF) return idx;
    }
    if (zone >= 1 && zone <= 9 && s->symbols) {
        u16 idx = chinese_font_symbol_index[(zone - 1) * 94 + (wei - 1)];
        if (idx != 0xFFFF) return idx | 0x8000;
    }
    g_misses++;
    return -1;
}

int chinese_font_lookup_unicode(chinese_font_handle_t slot, u32 cp) {
    const chinese_font_slot_t* s = slot_ok(slot);
    if (!s) return -3;
    g_lookups++;
    /* Binary search over the flat {unicode, idx} pair table. */
    u32 lo = 0, hi = s->uto_count;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        u16 key = s->uto_gb[2 * mid];
        if (key == cp) {
            return (int)s->uto_gb[2 * mid + 1];
        }
        if (key < cp) lo = mid + 1; else hi = mid;
    }
    g_misses++;
    return -1;
}

const u8* chinese_font_get_bitmap(chinese_font_handle_t slot, int idx) {
    const chinese_font_slot_t* s = slot_ok(slot);
    if (!s) return NULL;
    if (idx & 0x8000) {
        u32 i = (u32)(idx & 0x7FFF);
        if (!s->symbols || i >= s->symbol_count) return NULL;
        return &s->symbols[i * CHINESE_FONT_GLYPH_BYTES];
    }
    if (idx < 0 || (u32)idx >= s->hanzi_count) return NULL;
    return &s->hanzi[(u32)idx * CHINESE_FONT_GLYPH_BYTES];
}

const u8* chinese_font_get_bitmap_unicode(chinese_font_handle_t slot, u32 cp) {
    int idx = chinese_font_lookup_unicode(slot, cp);
    if (idx < 0) return NULL;
    return chinese_font_get_bitmap(slot, idx);
}

int chinese_font_get_metrics(u32* w, u32* h, u32* stride) {
    if (!w || !h || !stride) return -2;
    *w = CHINESE_FONT_GLYPH_W;
    *h = CHINESE_FONT_GLYPH_H;
    *stride = 2;
    return 0;
}

u32 chinese_font_index_to_unicode(chinese_font_handle_t slot, int idx) {
    const chinese_font_slot_t* s = slot_ok(slot);
    if (!s || !s->hanzi_count) return 0xFFFFFFFFu;
    u32 i;
    if (idx & 0x8000) {
        i = (u32)(idx & 0x7FFF);
        if (!s->symbols || i >= s->symbol_count) return 0xFFFFFFFFu;
        i += s->hanzi_count;
    } else {
        if (idx < 0 || (u32)idx >= s->hanzi_count) return 0xFFFFFFFFu;
        i = (u32)idx;
    }
    if (i >= s->hanzi_count + s->symbol_count) return 0xFFFFFFFFu;
    /* The embedded index table covers hanzi then symbols; runtime fonts
     * with a different layout fall back to the unicode reverse table. */
    if (s->hanzi_count + s->symbol_count == CHINESE_FONT_UTO_COUNT &&
        i < CHINESE_FONT_UTO_COUNT) {
        u16 v = chinese_font_idx_to_unicode[i];
        return (v & 0x8000) ? (v & 0x7FFF) : v;
    }
    /* Runtime font: rebuild from its own uto_gb (idx -> unicode scan). */
    for (u32 p = 0; p < s->uto_count; ++p) {
        u16 v = s->uto_gb[2 * p + 1];
        u32 vi = (v & 0x8000) ? ((u32)(v & 0x7FFF) + s->hanzi_count) : v;
        if (vi == i) return s->uto_gb[2 * p];
    }
    return 0xFFFFFFFFu;
}

int chinese_font_status(chinese_font_stats_t* out) {
    if (!out) return -2;
    if (!g_font_ready) chinese_font_init();
    spin_lock(&g_font_lock);
    u32 used = 0;
    for (u32 i = 0; i < CHINESE_FONT_MAX_SLOTS; ++i) {
        if (g_slots[i].in_use) used++;
    }
    out->hanzi_count      = g_slots[0].hanzi_count;
    out->symbol_count     = g_slots[0].symbol_count;
    out->glyph_bytes      = CHINESE_FONT_GLYPH_BYTES;
    out->hanzi_data_size  = g_slots[0].hanzi_count * CHINESE_FONT_GLYPH_BYTES;
    out->symbol_data_size = g_slots[0].symbol_count * CHINESE_FONT_GLYPH_BYTES;
    out->slots_used       = used;
    out->lookups          = g_lookups;
    out->misses           = g_misses;
    out->source           = g_slots[0].name;
    spin_unlock(&g_font_lock);
    return 0;
}
