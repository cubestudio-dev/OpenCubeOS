/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/encoding.h
 * Purpose: GB2312 / UTF-8 / Unicode decoding, encoding, conversion and
 *          detection.
 *
 * Internal representation for all conversions is a Unicode BMP
 * codepoint (u32).  GB2312 maps through the embedded font's reverse
 * tables (chinese_font_lookup / chinese_font_index_to_unicode), so the
 * encoder/decoder and the renderer always agree on the same charset.
 *
 * UTF-8 is decoded strictly per RFC 3629:
 *   - overlong encodings rejected
 *   - surrogates U+D800..U+DFFF rejected
 *   - > U+10FFFF rejected
 *   - truncated sequences rejected
 *
 * Driver-API style extension (the numbered L1 set stays closed at
 * 1-138).
 */
#ifndef OC_ENCODING_H
#define OC_ENCODING_H

#include "../kernel/types.h"

typedef enum encoding_kind {
    ENC_ASCII  = 0,   /* only 0x00..0x7F bytes present               */
    ENC_GB2312 = 1,   /* GB2312 double-byte + ASCII single-byte      */
    ENC_UTF8   = 2,   /* RFC 3629 UTF-8                              */
    ENC_MIXED  = 3    /* valid for >1 of the above / undecidable     */
} encoding_kind_t;

/* Error codes shared by the decode/encode/convert entry points. */
#define ENC_E_INVAL   (-2)   /* NULL pointer / zero capacity          */
#define ENC_E_TRUNC   (-3)   /* input ends mid-sequence               */
#define ENC_E_ILLEGAL (-4)   /* byte sequence not valid in the charset */
#define ENC_E_NOSPACE (-5)   /* output buffer too small               */
#define ENC_E_UNMAP   (-6)   /* codepoint has no mapping in the target */

/* Initialize counters/state. Idempotent. */
int encoding_init(void);

/* Decode ONE GB2312 character starting at in[0] (len bytes available).
 * ASCII bytes decode to themselves. On success *out_cp holds the
 * Unicode codepoint and *consumed the number of input bytes used
 * (1 or 2). */
int encoding_gb2312_decode(const u8* in, u32 len, u32* out_cp, u32* consumed);

/* Encode ONE codepoint as GB2312 (1 or 2 bytes). Returns ENC_E_UNMAP
 * when the codepoint is not in GB2312. */
int encoding_gb2312_encode(u32 cp, u8* out, u32 cap, u32* produced);

/* Decode ONE UTF-8 sequence (strict RFC 3629). ASCII decodes to
 * itself. */
int encoding_utf8_decode(const u8* in, u32 len, u32* out_cp, u32* consumed);

/* Encode ONE codepoint as UTF-8 (1..4 bytes). Surrogates are rejected. */
int encoding_utf8_encode(u32 cp, u8* out, u32 cap, u32* produced);

/* Bulk conversion. src/dst are encoding_kind_t; ASCII and UTF-8 are
 * handled byte-wise, GB2312 through the font tables. Mixed input is
 * treated as GB2312 for decoding (its ASCII subset matches). out_len
 * receives the produced byte count on success. */
int encoding_convert(const u8* in, u32 in_len,
                     encoding_kind_t src, encoding_kind_t dst,
                     u8* out, u32 cap, u32* out_len);

/* Heuristic detection: pure-ASCII data reports ENC_ASCII; data that
 * parses fully as strict UTF-8 with at least one multi-byte sequence
 * reports ENC_UTF8; data whose lead/trail bytes match the GB2312
 * double-byte ranges reports ENC_GB2312; anything else ENC_MIXED.
 * Empty input is ENC_ASCII. */
int encoding_detect(const u8* data, u32 len, encoding_kind_t* out);

/* One-line status: counts of decode/encode/conversion calls and the
 * charset coverage. Writes into buf (NUL-terminated); returns the
 * number of bytes written (excluding NUL), or -2 on NULL/zero cap. */
int encoding_status(char* buf, u32 cap);

#endif /* OC_ENCODING_H */
