/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/encoding.c
 * Purpose: GB2312 / UTF-8 / Unicode codec + conversion + detection.
 */
#include "encoding.h"
#include "chinese_font.h"
#include "../core/lib/lib_string.h"

static u32 g_decodes;
static u32 g_encodes;
static u32 g_converts;
static u32 g_detects;

int encoding_init(void) {
    g_decodes  = 0;
    g_encodes  = 0;
    g_converts = 0;
    g_detects  = 0;
    chinese_font_load();
    return 0;
}

/* ---------------------------------------------------------------- GB2312 */

int encoding_gb2312_decode(const u8* in, u32 len, u32* out_cp, u32* consumed) {
    if (!in || !out_cp || !consumed) return ENC_E_INVAL;
    if (len == 0) return ENC_E_TRUNC;
    g_decodes++;
    u8 b0 = in[0];
    if (b0 < 0x80) {                    /* ASCII pass-through */
        *out_cp = b0;
        *consumed = 1;
        return 0;
    }
    /* GB2312 double byte: lead 0xA1..0xF7, trail 0xA1..0xFE. */
    if (b0 < 0xA1 || b0 > 0xF7) return ENC_E_ILLEGAL;
    if (len < 2) return ENC_E_TRUNC;
    u8 b1 = in[1];
    if (b1 < 0xA1 || b1 > 0xFE) return ENC_E_ILLEGAL;
    u32 zone = b0 - 0xA0;
    u32 wei  = b1 - 0xA0;
    int idx = chinese_font_lookup(0, zone, wei);
    if (idx < 0) return ENC_E_ILLEGAL;
    u32 cp = chinese_font_index_to_unicode(0, idx);
    if (cp == 0xFFFFFFFFu) return ENC_E_ILLEGAL;
    *out_cp = cp;
    *consumed = 2;
    return 0;
}

int encoding_gb2312_encode(u32 cp, u8* out, u32 cap, u32* produced) {
    if (!out || !produced) return ENC_E_INVAL;
    g_encodes++;
    if (cp < 0x80) {
        if (cap < 1) return ENC_E_NOSPACE;
        out[0] = (u8)cp;
        *produced = 1;
        return 0;
    }
    if (cp > 0xFFFF) return ENC_E_UNMAP;
    int idx = chinese_font_lookup_unicode(0, cp);
    if (idx < 0) return ENC_E_UNMAP;
    /* Reverse the zone/wei slot from the compact index: walk the slot
     * table once per call is 6768 u16 reads worst case; keep it simple
     * and exact instead of carrying a third table. */
    int found = -1;
    if (idx & 0x8000) {
        extern const u16 chinese_font_symbol_index[846];
        u32 want = (u32)(idx & 0x7FFF);
        for (u32 s = 0; s < 846; ++s) {
            if (chinese_font_symbol_index[s] == want) {
                found = (int)s;   /* (zone-1)*94 + (wei-1) */
                break;
            }
        }
        if (found < 0) return ENC_E_UNMAP;
        u32 zone = (u32)found / 94 + 1;
        u32 wei  = (u32)found % 94 + 1;
        if (cap < 2) return ENC_E_NOSPACE;
        out[0] = (u8)(zone + 0xA0);
        out[1] = (u8)(wei + 0xA0);
        *produced = 2;
        return 0;
    }
    extern const u16 chinese_font_zone_index[6768];
    for (u32 s = 0; s < 6768; ++s) {
        if (chinese_font_zone_index[s] == (u16)idx) {
            found = (int)s;       /* (zone-16)*94 + (wei-1) */
            break;
        }
    }
    if (found < 0) return ENC_E_UNMAP;
    u32 zone = (u32)found / 94 + 16;
    u32 wei  = (u32)found % 94 + 1;
    if (cap < 2) return ENC_E_NOSPACE;
    out[0] = (u8)(zone + 0xA0);
    out[1] = (u8)(wei + 0xA0);
    *produced = 2;
    return 0;
}

/* ------------------------------------------------------------------ UTF-8 */

int encoding_utf8_decode(const u8* in, u32 len, u32* out_cp, u32* consumed) {
    if (!in || !out_cp || !consumed) return ENC_E_INVAL;
    if (len == 0) return ENC_E_TRUNC;
    g_decodes++;
    u8 b0 = in[0];
    u32 need, cp;
    if (b0 < 0x80) {
        *out_cp = b0;
        *consumed = 1;
        return 0;
    } else if ((b0 & 0xE0) == 0xC0) {
        need = 1; cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2; cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3; cp = b0 & 0x07;
    } else {
        return ENC_E_ILLEGAL;   /* 0x80..0xBF lead or 0xF8..0xFF */
    }
    if (len < need + 1) return ENC_E_TRUNC;
    for (u32 i = 1; i <= need; ++i) {
        u8 b = in[i];
        if ((b & 0xC0) != 0x80) return ENC_E_ILLEGAL;
        cp = (cp << 6) | (b & 0x3F);
    }
    /* RFC 3629: reject overlong, surrogates, out of range. */
    if (need == 1 && cp < 0x80) return ENC_E_ILLEGAL;
    if (need == 2 && cp < 0x800) return ENC_E_ILLEGAL;
    if (need == 3 && cp < 0x10000) return ENC_E_ILLEGAL;
    if (cp >= 0xD800 && cp <= 0xDFFF) return ENC_E_ILLEGAL;
    if (cp > 0x10FFFF) return ENC_E_ILLEGAL;
    *out_cp = cp;
    *consumed = need + 1;
    return 0;
}

int encoding_utf8_encode(u32 cp, u8* out, u32 cap, u32* produced) {
    if (!out || !produced) return ENC_E_INVAL;
    g_encodes++;
    if (cp >= 0xD800 && cp <= 0xDFFF) return ENC_E_ILLEGAL;
    if (cp > 0x10FFFF) return ENC_E_ILLEGAL;
    if (cp < 0x80) {
        if (cap < 1) return ENC_E_NOSPACE;
        out[0] = (u8)cp;
        *produced = 1;
        return 0;
    }
    if (cp < 0x800) {
        if (cap < 2) return ENC_E_NOSPACE;
        out[0] = (u8)(0xC0 | (cp >> 6));
        out[1] = (u8)(0x80 | (cp & 0x3F));
        *produced = 2;
        return 0;
    }
    if (cp < 0x10000) {
        if (cap < 3) return ENC_E_NOSPACE;
        out[0] = (u8)(0xE0 | (cp >> 12));
        out[1] = (u8)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (u8)(0x80 | (cp & 0x3F));
        *produced = 3;
        return 0;
    }
    if (cap < 4) return ENC_E_NOSPACE;
    out[0] = (u8)(0xF0 | (cp >> 18));
    out[1] = (u8)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (u8)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (u8)(0x80 | (cp & 0x3F));
    *produced = 4;
    return 0;
}

/* --------------------------------------------------------------- convert */

int encoding_convert(const u8* in, u32 in_len,
                     encoding_kind_t src, encoding_kind_t dst,
                     u8* out, u32 cap, u32* out_len) {
    if (!in || !out || !out_len) return ENC_E_INVAL;
    if (src == ENC_MIXED || dst == ENC_MIXED) return ENC_E_INVAL;
    g_converts++;
    u32 ip = 0, op = 0;
    if (src == dst) {                       /* byte copy */
        if (in_len > cap) return ENC_E_NOSPACE;
        for (u32 i = 0; i < in_len; ++i) out[i] = in[i];
        *out_len = in_len;
        return 0;
    }
    while (ip < in_len) {
        u32 cp, consumed;
        int rc;
        if (src == ENC_UTF8) {
            rc = encoding_utf8_decode(in + ip, in_len - ip, &cp, &consumed);
        } else {
            /* GB2312 (and ASCII, which is its subset) */
            rc = encoding_gb2312_decode(in + ip, in_len - ip, &cp, &consumed);
        }
        if (rc != 0) return rc;
        u8 tmp[4];
        u32 produced = 0;
        if (dst == ENC_ASCII) {
            if (cp >= 0x80) return ENC_E_UNMAP;
            tmp[0] = (u8)cp;
            produced = 1;
        } else if (dst == ENC_UTF8) {
            rc = encoding_utf8_encode(cp, tmp, 4, &produced);
        } else {
            rc = encoding_gb2312_encode(cp, tmp, 4, &produced);
        }
        if (rc != 0) return rc;
        if (op + produced > cap) return ENC_E_NOSPACE;
        for (u32 i = 0; i < produced; ++i) out[op++] = tmp[i];
        ip += consumed;
    }
    *out_len = op;
    return 0;
}

/* --------------------------------------------------------------- detect */

static int all_ascii(const u8* d, u32 len) {
    for (u32 i = 0; i < len; ++i) {
        if (d[i] >= 0x80) return 0;
    }
    return 1;
}

int encoding_detect(const u8* data, u32 len, encoding_kind_t* out) {
    if (!data || !out) return ENC_E_INVAL;
    g_detects++;
    if (len == 0 || all_ascii(data, len)) {
        *out = ENC_ASCII;
        return 0;
    }
    /* Full strict UTF-8 parse: if it decodes cleanly (>=1 multibyte
     * sequence) it is UTF-8. GB2312 text can also be valid UTF-8 only
     * by coincidence (lead bytes 0xA1..0xF7 are continuation bytes in
     * UTF-8, so a true GB2312 stream is almost never valid UTF-8). */
    u32 ip = 0, multibyte = 0, bad_utf8 = 0;
    while (ip < len) {
        u32 cp, consumed;
        if (encoding_utf8_decode(data + ip, len - ip, &cp, &consumed) != 0) {
            bad_utf8 = 1;
            break;
        }
        if (consumed > 1) multibyte++;
        ip += consumed;
    }
    if (!bad_utf8 && multibyte > 0) {
        *out = ENC_UTF8;
        return 0;
    }
    /* GB2312 shape check: every byte >= 0x80 must be a valid GB2312
     * lead followed by a valid trail. */
    u32 i = 0, gb_ok = 0;
    while (i < len) {
        u8 b = data[i];
        if (b < 0x80) { i++; continue; }
        if (b < 0xA1 || b > 0xF7 || i + 1 >= len) { gb_ok = 0; break; }
        u8 t = data[i + 1];
        if (t < 0xA1 || t > 0xFE) { gb_ok = 0; break; }
        gb_ok = 1;
        i += 2;
    }
    if (gb_ok) {
        *out = ENC_GB2312;
        return 0;
    }
    *out = ENC_MIXED;
    return 0;
}

/* ---------------------------------------------------------------- status */

/* Bounded append helpers (kernel has no snprintf). */
static u32 st_cat(char* buf, u32 cap, u32 pos, const char* s) {
    while (*s && pos + 1 < cap) buf[pos++] = *s++;
    return pos;
}

static u32 st_cat_u32(char* buf, u32 cap, u32 pos, u32 v) {
    char tmp[12];
    usize n = u64_to_str(v, tmp);
    for (usize i = 0; i < n && pos + 1 < cap; ++i) buf[pos++] = tmp[i];
    return pos;
}

int encoding_status(char* buf, u32 cap) {
    if (!buf || cap == 0) return -2;
    chinese_font_stats_t st;
    if (chinese_font_status(&st) != 0) return -2;
    u32 pos = 0;
    pos = st_cat(buf, cap, pos, "encoding: gb2312+utf8+ascii | font: ");
    pos = st_cat(buf, cap, pos, st.source ? st.source : "-");
    pos = st_cat(buf, cap, pos, " | hanzi ");
    pos = st_cat_u32(buf, cap, pos, st.hanzi_count);
    pos = st_cat(buf, cap, pos, " | symbols ");
    pos = st_cat_u32(buf, cap, pos, st.symbol_count);
    pos = st_cat(buf, cap, pos, " | decode ");
    pos = st_cat_u32(buf, cap, pos, g_decodes);
    pos = st_cat(buf, cap, pos, " | encode ");
    pos = st_cat_u32(buf, cap, pos, g_encodes);
    pos = st_cat(buf, cap, pos, " | convert ");
    pos = st_cat_u32(buf, cap, pos, g_converts);
    pos = st_cat(buf, cap, pos, " | detect ");
    pos = st_cat_u32(buf, cap, pos, g_detects);
    pos = st_cat(buf, cap, pos, " | miss ");
    pos = st_cat_u32(buf, cap, pos, st.misses);
    buf[pos] = 0;
    return (int)pos;
}
