/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/gzip.c
 * Purpose: gzip (RFC 1952) + DEFLATE (RFC 1951) inflate, see gzip.h.
 *
 * Implementation notes:
 *   - Fully resumable: every multi-bit read keeps its accumulator
 *     (d_acc + acc_bits) and the Huffman decoder keeps (hd_len, hd_code,
 *     hd_first, hd_index) in the engine, so oc_gzip_feed() may be called
 *     with arbitrarily sized chunks - tar.gz packages are streamed from
 *     disk or network in small blocks.
 *   - Canonical Huffman decoding uses the count/symbol scheme (zlib's
 *     "puff" style): simple, compact and easy to verify.
 *   - Every decompressed byte passes through the 32 KiB LZ77 window
 *     (so match distances always resolve) and a 512-byte staging buffer
 *     flushed to the user sink.
 */
#include "gzip.h"
#include "heap.h"
#include "string.h"

#include <stdint.h>

/* inflate state machine states */
#define OC_GZS_HEADER       0    /* gzip container header             */
#define OC_GZS_BFINAL       1    /* 3-bit block header (BFINAL+BTYPE) */
#define OC_GZS_STORED_LEN   2    /* stored: align + LEN + NLEN        */
#define OC_GZS_STORED_DATA  3    /* stored: raw bytes                 */
#define OC_GZS_TABLE_SIZES  4    /* dynamic: HLIT/HDIST/HCLEN         */
#define OC_GZS_TABLE_CL     5    /* dynamic: 19x3bit code lengths     */
#define OC_GZS_TABLE_LENS   6    /* dynamic: lit/dist code lengths    */
#define OC_GZS_DECODE       7    /* main literal/length/distance loop */
#define OC_GZS_TRAILER      8    /* gzip CRC32 + ISIZE                */
#define OC_GZS_DONE         9

/* gzip header sub-states (h_sub) */
enum {
    GH_MAGIC0 = 0, GH_MAGIC1, GH_CM, GH_FLG, GH_MTIME, GH_XFL, GH_OS,
    GH_XLEN0, GH_XLEN1, GH_EXTRA, GH_NAME, GH_COMMENT, GH_HCRC,
    GH_DONE_SUB
};

/* dynamic table construction phases */
#define TP_SINGLE 0              /* one code length              */
#define TP_REPEAT 1              /* inside a 16/17/18 repeat run */

/* decode phases inside OC_GZS_DECODE */
#define DP_LITLEN    0           /* decode literal/length symbol */
#define DP_LENESEG   1           /* consume length extra bits    */
#define DP_DSTSYM    2           /* decode distance symbol       */
#define DP_DSTSEG    3           /* consume distance extra bits  */
#define DP_COPY      4           /* copy match from window       */

/* stored-length sub-phase: which of the 4 bytes LEN/NLEN we are in */
#define SL_LEN0 0
#define SL_LEN1 1
#define SL_NLEN0 2
#define SL_NLEN1 3

/* length base/extra tables (RFC 1951 3.2.5) */
static const u16 g_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,
    163,195,227,258
};
static const u8 g_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
/* distance base/extra tables */
static const u16 g_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,
    2049,3073,4097,6145,8193,12289,16385,24577
};
static const u8 g_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
/* code length alphabet order (RFC 1951 3.2.7) */
static const u8 g_cl_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

/* ---- CRC32 --------------------------------------------------------- */

static u32 g_crc_table[256];
static int  g_crc_ready = 0;

static void crc32_build_table(void) {
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[i] = c;
    }
    g_crc_ready = 1;
}

u32 oc_crc32(const void *data, int len) {
    if (!g_crc_ready) crc32_build_table();
    const u8 *p = (const u8 *)data;
    u32 c = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++)
        c = g_crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* streaming CRC update (raw, keeps the pre-final-xor form) */
static u32 crc32_update(u32 crc, const u8 *p, int len) {
    if (!g_crc_ready) crc32_build_table();
    u32 c = crc;
    for (int i = 0; i < len; i++)
        c = g_crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c;
}

/* ---- bit reader ---------------------------------------------------- */

/* Read one bit: 0/1 on success, -1 when input is exhausted. */
static int read_bit(oc_gzip_t *g) {
    if (g->bitcnt == 0) {
        if (g->in_pos >= g->in_len) return -1;
        g->bitbuf = (u32)g->in[g->in_pos++];
        g->bitcnt = 8;
    }
    int bit = (int)(g->bitbuf & 1u);
    g->bitbuf >>= 1;
    g->bitcnt--;
    return bit;
}

/* Read n bits (LSB first).  *acc accumulates across resume attempts;
 * g->acc_bits tracks how many bits of THIS field were already read.
 * Returns 0 when the full field is in *acc, -1 when input ran out
 * (state saved; call again with the same acc after more input). */
static int read_bits(oc_gzip_t *g, int n, u32 *acc) {
    if (g->acc_bits == 0) *acc = 0;
    for (int i = g->acc_bits; i < n; i++) {
        int b = read_bit(g);
        if (b < 0) {
            g->acc_bits = i;
            return -1;
        }
        *acc |= ((u32)b) << i;
    }
    g->acc_bits = 0;
    return 0;
}

/* ---- output path --------------------------------------------------- */

/* Flush the staging buffer to the sink + CRC. 0 ok, <0 sink error. */
static int emit_flush(oc_gzip_t *g) {
    if (g->out_len == 0) return 0;
    u8 *p = g->out_buf;
    int n = g->out_len;
    g->out_len = 0;
    g->crc = crc32_update(g->crc, p, n);
    g->out_bytes += (u32)n;
    if (g->sink && g->sink(g->sink_ctx, p, n) != 0) {
        g->err = OC_GZIP_ERR_SINK;
        return g->err;
    }
    return 0;
}

/* Emit one decompressed byte into the window + staging buffer. */
static int emit_byte(oc_gzip_t *g, u8 b) {
    g->window[g->wpos] = b;
    g->wpos = (g->wpos + 1) & 32767;
    if (g->window_valid < 32768) g->window_valid++;
    g->out_buf[g->out_len++] = b;
    if (g->out_len == (int)sizeof(g->out_buf)) {
        return emit_flush(g);
    }
    return 0;
}

/* ---- Huffman tables ------------------------------------------------- */

/* Build a canonical decode table from code lengths.  Returns 0 on
 * success, OC_GZIP_ERR_TABLE on over-subscribed tables.  allow_empty
 * accepts an all-zero length set (legal for the distance table: RFC 1951
 * allows a block that uses no matches at all). */
static int huff_build(short *counts, short *symbols, const short *lens,
                     int n, int allow_empty) {
    for (int i = 0; i <= 15; i++) counts[i] = 0;
    for (int i = 0; i < n; i++) counts[lens[i]]++;
    if (counts[0] == n && !allow_empty) return OC_GZIP_ERR_TABLE;
    int left = 1;
    for (int l = 1; l <= 15; l++) {
        left <<= 1;
        left -= counts[l];
        if (left < 0) return OC_GZIP_ERR_TABLE;
    }
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (short)(offs[l] + counts[l]);
    for (int i = 0; i < n; i++) {
        if (lens[i] != 0) {
            symbols[offs[lens[i]]] = (short)i;
            offs[lens[i]]++;
        }
    }
    return 0;
}

/* Decode one symbol with a count/symbol table.  Returns symbol >= 0,
 * -1 need more input (position saved in hd_*), OC_GZIP_ERR_TABLE on an
 * invalid code. */
static int huff_decode(oc_gzip_t *g, const short *counts, const short *symbols) {
    if (g->hd_len == 0) {
        g->hd_code = 0;
        g->hd_first = 0;
        g->hd_index = 0;
        g->hd_len = 1;
    }
    for (; g->hd_len <= 15; g->hd_len++) {
        int b = read_bit(g);
        if (b < 0) return -1;
        g->hd_code |= b;
        int count = counts[g->hd_len];
        if (g->hd_code - count < g->hd_first) {
            int sym = symbols[g->hd_index + (g->hd_code - g->hd_first)];
            g->hd_len = 0;                    /* reset for next symbol */
            return sym;
        }
        g->hd_index += count;
        g->hd_first += count;
        g->hd_first <<= 1;
        g->hd_code <<= 1;
    }
    g->hd_len = 0;
    return OC_GZIP_ERR_TABLE;
}

/* ---- gzip header ---------------------------------------------------- */

/* Read one whole header byte (resumable through d_acc + acc_bits).
 * Returns 0 ok (byte in *out), -1 need more input. */
static int header_byte(oc_gzip_t *g, u8 *out) {
    u32 acc = g->d_acc;
    if (read_bits(g, 8, &acc) != 0) {
        g->d_acc = acc;
        return -1;
    }
    g->d_acc = 0;
    *out = (u8)acc;
    return 0;
}

/* Parse the gzip header.  Returns 1 = complete, 0 = need more input,
 * <0 = error. */
static int do_header(oc_gzip_t *g) {
    for (;;) {
        u8 b;
        switch (g->h_sub) {
        case GH_MAGIC0:
        case GH_MAGIC1:
        case GH_CM:
        case GH_XFL: {
            if (header_byte(g, &b) < 0) return 0;
            if (g->h_sub == GH_MAGIC0 && b != 0x1F) return OC_GZIP_ERR_MAGIC;
            if (g->h_sub == GH_MAGIC1 && b != 0x8B) return OC_GZIP_ERR_MAGIC;
            if (g->h_sub == GH_CM && b != 8) return OC_GZIP_ERR_METHOD;
            g->h_sub++;
            break;
        }
        case GH_OS: {
            if (header_byte(g, &b) < 0) return 0;
            /* the fixed header ends here; FLG decides which optional
             * fields follow (or none at all) */
            int flg = g->h_flg;
            if (flg & 0x04) g->h_sub = GH_XLEN0;
            else if (flg & 0x08) g->h_sub = GH_NAME;
            else if (flg & 0x10) g->h_sub = GH_COMMENT;
            else if (flg & 0x02) g->h_sub = GH_HCRC;
            else g->h_sub = GH_DONE_SUB;
            break;
        }
        case GH_FLG: {
            if (header_byte(g, &b) < 0) return 0;
            if (b & 0xE0) return OC_GZIP_ERR_HEADER;   /* reserved bits */
            g->h_flg = b;
            g->h_sub = GH_MTIME;
            g->h_cnt = 0;
            break;
        }
        case GH_MTIME: {
            if (header_byte(g, &b) < 0) return 0;
            if (++g->h_cnt >= 4) { g->h_sub = GH_XFL; g->h_cnt = 0; }
            break;
        }
        case GH_XLEN0:
        case GH_XLEN1: {
            if (header_byte(g, &b) < 0) return 0;
            g->h_xlen |= ((u32)b) << (g->h_sub == GH_XLEN0 ? 0 : 8);
            if (g->h_sub == GH_XLEN1) {
                if (g->h_xlen != 0) {
                    g->h_sub = GH_EXTRA;
                } else {
                    int flg = g->h_flg;
                    if (flg & 0x08) g->h_sub = GH_NAME;
                    else if (flg & 0x10) g->h_sub = GH_COMMENT;
                    else if (flg & 0x02) g->h_sub = GH_HCRC;
                    else g->h_sub = GH_DONE_SUB;
                }
            } else {
                g->h_sub = GH_XLEN1;
            }
            break;
        }
        case GH_EXTRA: {
            if (header_byte(g, &b) < 0) return 0;
            if (g->h_xlen > 0) g->h_xlen--;
            if (g->h_xlen == 0) {
                int flg = g->h_flg;
                if (flg & 0x08) g->h_sub = GH_NAME;
                else if (flg & 0x10) g->h_sub = GH_COMMENT;
                else if (flg & 0x02) g->h_sub = GH_HCRC;
                else g->h_sub = GH_DONE_SUB;
            }
            break;
        }
        case GH_NAME:
        case GH_COMMENT: {
            if (header_byte(g, &b) < 0) return 0;
            if (b == 0) {
                int flg = g->h_flg;
                if (g->h_sub == GH_NAME) {
                    if (flg & 0x10) g->h_sub = GH_COMMENT;
                    else if (flg & 0x02) g->h_sub = GH_HCRC;
                    else g->h_sub = GH_DONE_SUB;
                } else {
                    if (flg & 0x02) g->h_sub = GH_HCRC;
                    else g->h_sub = GH_DONE_SUB;
                }
            }
            break;
        }
        case GH_HCRC: {
            if (header_byte(g, &b) < 0) return 0;
            if (++g->h_cnt >= 2) {
                g->h_cnt = 0;
                g->h_sub = GH_DONE_SUB;
            }
            break;
        }
        case GH_DONE_SUB:
            return 1;
        default:
            return OC_GZIP_ERR_HEADER;
        }
    }
}

/* ---- stored block ---------------------------------------------------- */

/* Read LEN + NLEN (4 bytes, byte-aligned).  Returns 1 = lengths ok,
 * 0 = need more input, <0 = error. */
static int do_stored_len(oc_gzip_t *g) {
    g->bitcnt = 0;                     /* align to byte boundary */
    g->bitbuf = 0;
    for (;;) {
        if (g->stored_len_read >= 4) break;
        u32 acc = g->d_acc;
        if (read_bits(g, 8, &acc) != 0) {
            g->d_acc = acc;
            return 0;
        }
        g->d_acc = 0;
        u8 b = (u8)acc;
        switch (g->stored_len_read) {
        case SL_LEN0:  g->stored_len_acc  = b; break;
        case SL_LEN1:  g->stored_len_acc |= ((u32)b) << 8; break;
        case SL_NLEN0: g->stored_nlen_acc = b; break;
        default:       g->stored_nlen_acc |= ((u32)b) << 8; break;
        }
        g->stored_len_read++;
    }
    if (((g->stored_len_acc ^ 0xFFFFu) & 0xFFFFu) != g->stored_nlen_acc)
        return OC_GZIP_ERR_BLOCK;
    g->stored_left = g->stored_len_acc;
    return 1;
}

/* ---- dynamic table construction -------------------------------------- */

static int build_tables(oc_gzip_t *g) {
    int rc = huff_build(g->counts_lit, g->syms_lit, g->lens_lit, g->hlit, 0);
    if (rc != 0) return rc;
    rc = huff_build(g->counts_dst, g->syms_dst, g->lens_dst, g->hdist, 1);
    if (rc != 0) return rc;
    g->tables_kind = 2;
    g->d_phase = DP_LITLEN;
    g->state = OC_GZS_DECODE;
    return 1;
}

/* Decode the lit/dist code lengths with the CL table (which lives in
 * counts_lit/syms_lit at this point).  Returns 1 = tables built,
 * 0 = need more input, <0 = error. */
static int do_table_lens(oc_gzip_t *g) {
    for (;;) {
        if (g->t_phase == TP_REPEAT) {
            while (g->t_repeat_left > 0 && g->t_idx < g->hlit + g->hdist) {
                short *dst = (g->t_idx < g->hlit) ? g->lens_lit : g->lens_dst;
                int di = (g->t_idx < g->hlit) ? g->t_idx : g->t_idx - g->hlit;
                dst[di] = (short)g->t_repeat_val;
                g->t_idx++;
                g->t_repeat_left--;
            }
            if (g->t_repeat_left > 0) return 0;
            g->t_phase = TP_SINGLE;
            if (g->t_idx >= g->hlit + g->hdist) return build_tables(g);
            continue;
        }
        int sym = huff_decode(g, g->counts_lit, g->cl_syms);
        if (sym == -1) return 0;
        if (sym == OC_GZIP_ERR_TABLE) return OC_GZIP_ERR_TABLE;
        if (sym < 16) {
            short *dst = (g->t_idx < g->hlit) ? g->lens_lit : g->lens_dst;
            int di = (g->t_idx < g->hlit) ? g->t_idx : g->t_idx - g->hlit;
            dst[di] = (short)sym;
            g->t_idx++;
        } else if (sym == 16) {
            if (g->t_idx == 0) return OC_GZIP_ERR_TABLE;
            short prev = (g->t_idx - 1 < g->hlit)
                       ? g->lens_lit[g->t_idx - 1]
                       : g->lens_dst[g->t_idx - 1 - g->hlit];
            g->t_repeat_val = prev;
            u32 acc = g->d_acc;
            if (read_bits(g, 2, &acc) != 0) { g->d_acc = acc; return 0; }
            g->d_acc = 0;
            g->t_repeat_left = 3 + (int)acc;
            g->t_phase = TP_REPEAT;
            continue;
        } else if (sym == 17) {
            g->t_repeat_val = 0;
            u32 acc = g->d_acc;
            if (read_bits(g, 3, &acc) != 0) { g->d_acc = acc; return 0; }
            g->d_acc = 0;
            g->t_repeat_left = 3 + (int)acc;
            g->t_phase = TP_REPEAT;
            continue;
        } else { /* 18 */
            g->t_repeat_val = 0;
            u32 acc = g->d_acc;
            if (read_bits(g, 7, &acc) != 0) { g->d_acc = acc; return 0; }
            g->d_acc = 0;
            g->t_repeat_left = 11 + (int)acc;
            g->t_phase = TP_REPEAT;
            continue;
        }
        if (g->t_idx >= g->hlit + g->hdist) return build_tables(g);
    }
}

/* ---- main LZ77 decode ------------------------------------------------- */

static int do_decode(oc_gzip_t *g) {
    for (;;) {
        switch (g->d_phase) {
        case DP_LITLEN: {
            int sym = huff_decode(g, g->counts_lit, g->syms_lit);
            if (sym == -1) return 0;
            if (sym == OC_GZIP_ERR_TABLE) return OC_GZIP_ERR_TABLE;
            if (sym < 256) {
                int rc = emit_byte(g, (u8)sym);
                if (rc != 0) return rc;
                break;
            }
            if (sym == 256) {                       /* end of block */
                g->state = OC_GZS_BFINAL;
                return 1;
            }
            int li = sym - 257;
            if (li >= 29) return OC_GZIP_ERR_TABLE;
            g->d_len = g_len_base[li];
            g->d_extra_left = g_len_extra[li];
            g->d_phase = g->d_extra_left > 0 ? DP_LENESEG : DP_DSTSYM;
            g->d_acc = 0;
            break;
        }
        case DP_LENESEG: {
            u32 acc = g->d_acc;
            if (read_bits(g, g->d_extra_left, &acc) != 0) {
                g->d_acc = acc;
                return 0;
            }
            g->d_acc = 0;
            g->d_len += acc;
            g->d_phase = DP_DSTSYM;
            break;
        }
        case DP_DSTSYM: {
            int sym = huff_decode(g, g->counts_dst, g->syms_dst);
            if (sym == -1) return 0;
            if (sym == OC_GZIP_ERR_TABLE) return OC_GZIP_ERR_TABLE;
            if (sym >= 30) return OC_GZIP_ERR_TABLE;
            g->d_dist = g_dist_base[sym];
            g->d_extra_left = g_dist_extra[sym];
            g->d_phase = g->d_extra_left > 0 ? DP_DSTSEG : DP_COPY;
            g->d_acc = 0;
            break;
        }
        case DP_DSTSEG: {
            u32 acc = g->d_acc;
            if (read_bits(g, g->d_extra_left, &acc) != 0) {
                g->d_acc = acc;
                return 0;
            }
            g->d_acc = 0;
            g->d_dist += acc;
            g->d_phase = DP_COPY;
            break;
        }
        case DP_COPY: {
            if (g->d_dist > (u32)g->window_valid) return OC_GZIP_ERR_DIST;
            u32 left = g->d_len;
            while (left > 0) {
                int src = (g->wpos - (int)g->d_dist) & 32767;
                u8 b = g->window[src];
                int rc = emit_byte(g, b);
                if (rc != 0) return rc;
                left--;
            }
            g->d_phase = DP_LITLEN;
            break;
        }
        default:
            return OC_GZIP_ERR_STATE;
        }
    }
}

/* ---- build the fixed Huffman tables ------------------------------------ */

static int build_fixed_tables(oc_gzip_t *g) {
    for (int i = 0; i <= 143; i++) g->lens_lit[i] = 8;
    for (int i = 144; i <= 255; i++) g->lens_lit[i] = 9;
    for (int i = 256; i <= 279; i++) g->lens_lit[i] = 7;
    for (int i = 280; i <= 287; i++) g->lens_lit[i] = 8;
    for (int i = 0; i < 30; i++) g->lens_dst[i] = 5;
    g->hlit = 288;
    g->hdist = 30;
    int rc = huff_build(g->counts_lit, g->syms_lit, g->lens_lit, 288, 0);
    if (rc != 0) return rc;
    rc = huff_build(g->counts_dst, g->syms_dst, g->lens_dst, 30, 1);
    if (rc != 0) return rc;
    g->tables_kind = 1;
    g->d_phase = DP_LITLEN;
    g->state = OC_GZS_DECODE;
    return 1;
}

/* ---- public API --------------------------------------------------------- */

int oc_gzip_init(oc_gzip_t *g, oc_gzip_sink_fn sink, void *sink_ctx) {
    if (!g || !sink) return OC_GZIP_ERR_ARGS;
    /* struct must be zeroed by the caller; (re)initialise every field
     * we own so re-init after memset(0) stays a no-op */
    g->sink = sink;
    g->sink_ctx = sink_ctx;
    g->bitbuf = 0;
    g->bitcnt = 0;
    g->in = 0;
    g->in_len = 0;
    g->in_pos = 0;
    g->state = OC_GZS_HEADER;
    g->last_block = 0;
    g->btype = 0;
    g->d_acc = 0;
    g->acc_bits = 0;
    g->stored_left = 0;
    g->stored_len_read = 0;
    g->stored_len_acc = 0;
    g->stored_nlen_acc = 0;
    g->tables_kind = 0;
    g->hlit = 0;
    g->hdist = 0;
    g->hclen = 0;
    g->t_idx = 0;
    g->t_phase = TP_SINGLE;
    g->t_repeat_left = 0;
    g->t_repeat_val = 0;
    g->hd_len = 0;
    g->hd_code = 0;
    g->hd_first = 0;
    g->hd_index = 0;
    g->h_sub = GH_MAGIC0;
    g->h_xlen = 0;
    g->h_cnt = 0;
    g->d_phase = DP_LITLEN;
    g->d_len = 0;
    g->d_dist = 0;
    g->d_extra_left = 0;
    g->wpos = 0;
    g->window_valid = 0;
    g->out_len = 0;
    g->crc = 0xFFFFFFFFu;
    g->out_bytes = 0;
    g->trailer_left = 0;
    g->done = 0;
    g->err = OC_GZIP_ERR_NONE;
    return 0;
}

int oc_gzip_feed(oc_gzip_t *g, const u8 *data, int len) {
    if (!g || (!data && len > 0) || len < 0) return OC_GZIP_ERR_ARGS;
    if (g->done) return 0;
    if (g->err != OC_GZIP_ERR_NONE) return g->err;

    g->in = data;
    g->in_len = len;
    g->in_pos = 0;

    int running = 1;
    while (running) {
        switch (g->state) {
        case OC_GZS_HEADER: {
            int rc = do_header(g);
            if (rc < 0) { g->err = rc; return rc; }
            if (rc == 1) g->state = OC_GZS_BFINAL;
            else running = 0;                       /* need more input */
            break;
        }
        case OC_GZS_BFINAL: {
            u32 acc = g->d_acc;
            if (read_bits(g, 3, &acc) != 0) {       /* need more input */
                g->d_acc = acc;
                running = 0;
                break;
            }
            g->d_acc = 0;
            g->last_block = (int)(acc & 1u);
            g->btype = (int)((acc >> 1) & 3u);
            if (g->btype == 0) {
                g->stored_len_read = 0;
                g->stored_len_acc = 0;
                g->stored_nlen_acc = 0;
                g->state = OC_GZS_STORED_LEN;
            } else if (g->btype == 1) {
                if (g->tables_kind != 1) {
                    int rc = build_fixed_tables(g);
                    if (rc < 0) { g->err = rc; return rc; }
                } else {
                    g->d_phase = DP_LITLEN;
                    g->state = OC_GZS_DECODE;
                }
            } else if (g->btype == 2) {
                g->state = OC_GZS_TABLE_SIZES;
                g->tables_kind = 0;
            } else {
                g->err = OC_GZIP_ERR_BLOCK;
                return g->err;
            }
            break;
        }
        case OC_GZS_STORED_LEN: {
            int rc = do_stored_len(g);
            if (rc < 0) { g->err = rc; return rc; }
            if (rc == 1) {
                if (g->stored_left == 0) {
                    g->state = OC_GZS_BFINAL;       /* empty stored block */
                } else {
                    g->state = OC_GZS_STORED_DATA;
                }
            } else {
                running = 0;
            }
            break;
        }
        case OC_GZS_STORED_DATA: {
            while (g->stored_left > 0 && g->in_pos < g->in_len) {
                u8 b = g->in[g->in_pos++];
                int rc = emit_byte(g, b);
                if (rc != 0) { g->err = rc; return rc; }
                g->stored_left--;
            }
            if (g->stored_left == 0) {
                g->state = OC_GZS_BFINAL;
            } else {
                running = 0;
            }
            break;
        }
        case OC_GZS_TABLE_SIZES: {
            u32 acc = g->d_acc;
            /* HLIT(5)+HDIST(5)+HCLEN(4) = 14 bits */
            if (read_bits(g, 14, &acc) != 0) {
                g->d_acc = acc;
                running = 0;
                break;
            }
            g->d_acc = 0;
            g->hlit = (int)(acc & 0x1Fu) + 257;
            g->hdist = (int)((acc >> 5) & 0x1Fu) + 1;
            g->hclen = (int)((acc >> 10) & 0xFu) + 4;
            if (g->hlit > 288 || g->hdist > 32) {
                g->err = OC_GZIP_ERR_TABLE;
                return g->err;
            }
            for (int i = 0; i < 19; i++) g->lens_cl[i] = 0;
            g->t_idx = 0;
            g->state = OC_GZS_TABLE_CL;
            break;
        }
        case OC_GZS_TABLE_CL: {
            while (g->t_idx < g->hclen) {
                u32 acc = g->d_acc;
                if (read_bits(g, 3, &acc) != 0) {
                    g->d_acc = acc;
                    running = 0;
                    break;
                }
                g->d_acc = 0;
                g->lens_cl[g_cl_order[g->t_idx]] = (short)acc;
                g->t_idx++;
            }
            if (running && g->t_idx >= g->hclen) {
                /* build the CL table into counts_lit/syms_lit; the
                 * lit/dist lengths will be decoded with it, then the
                 * real literal table overwrites counts_lit/syms_lit */
                int rc = huff_build(g->counts_lit, g->cl_syms, g->lens_cl, 19, 0);
                if (rc != 0) { g->err = rc; return rc; }
                g->t_idx = 0;
                g->t_phase = TP_SINGLE;
                g->t_repeat_left = 0;
                g->t_repeat_val = 0;
                for (int i = 0; i < 288; i++) g->lens_lit[i] = 0;
                for (int i = 0; i < 32; i++) g->lens_dst[i] = 0;
                g->state = OC_GZS_TABLE_LENS;
            }
            break;
        }
        case OC_GZS_TABLE_LENS: {
            int rc = do_table_lens(g);
            if (rc < 0) { g->err = rc; return rc; }
            if (rc == 0) running = 0;
            /* rc == 1: tables built, state == OC_GZS_DECODE */
            break;
        }
        case OC_GZS_DECODE: {
            int rc = do_decode(g);
            if (rc < 0) { g->err = rc; return rc; }
            if (rc == 0) running = 0;
            /* rc == 1: end of block -> state == OC_GZS_BFINAL */
            break;
        }
        case OC_GZS_TRAILER: {
            while (g->trailer_left < 8 && g->in_pos < g->in_len) {
                g->trailer[g->trailer_left++] = g->in[g->in_pos++];
            }
            if (g->trailer_left == 8) {
                u32 crc = (u32)g->trailer[0] |
                          ((u32)g->trailer[1] << 8) |
                          ((u32)g->trailer[2] << 16) |
                          ((u32)g->trailer[3] << 24);
                u32 isize = (u32)g->trailer[4] |
                            ((u32)g->trailer[5] << 8) |
                            ((u32)g->trailer[6] << 16) |
                            ((u32)g->trailer[7] << 24);
                u32 got = g->crc ^ 0xFFFFFFFFu;
                if (crc != got) { g->err = OC_GZIP_ERR_CRC; return g->err; }
                if (isize != (g->out_bytes & 0xFFFFFFFFu)) {
                    g->err = OC_GZIP_ERR_ISIZE;
                    return g->err;
                }
                g->done = 1;
                g->state = OC_GZS_DONE;
            }
            running = 0;
            break;
        }
        case OC_GZS_DONE:
            running = 0;
            break;
        default:
            g->err = OC_GZIP_ERR_STATE;
            return g->err;
        }

        /* the final block's body just completed -> gzip trailer.
         * Flush the staged output FIRST: the CRC must cover every
         * decompressed byte before the trailer check runs. */
        if (running && g->state == OC_GZS_BFINAL && g->last_block) {
            if (emit_flush(g) != 0) return g->err;
            g->state = OC_GZS_TRAILER;
            g->trailer_left = 0;
        }
    }

    /* flush any staged output before returning */
    if (g->out_len > 0 && g->err == OC_GZIP_ERR_NONE) {
        if (emit_flush(g) != 0) return g->err;
    }
    return (g->err != OC_GZIP_ERR_NONE) ? g->err : 0;
}

int oc_gzip_finish(oc_gzip_t *g) {
    if (!g) return OC_GZIP_ERR_ARGS;
    if (g->err != OC_GZIP_ERR_NONE) return g->err;
    if (!g->done) return OC_GZIP_ERR_TRUNC;
    return 0;
}

/* ---- one-shot helper ---------------------------------------------------- */

typedef struct {
    u8  *out;
    int  cap;
    int  len;
} one_shot_ctx_t;

static int one_shot_sink(void *ctx, const u8 *data, int len) {
    one_shot_ctx_t *c = (one_shot_ctx_t *)ctx;
    if (c->len + len > c->cap) return -1;
    for (int i = 0; i < len; i++) c->out[c->len++] = data[i];
    return 0;
}

int oc_gunzip(const u8 *in, int in_len, u8 *out, int out_cap) {
    if (!in || in_len <= 0 || !out || out_cap <= 0) return OC_GZIP_ERR_ARGS;
    one_shot_ctx_t ctx;
    ctx.out = out;
    ctx.cap = out_cap;
    ctx.len = 0;
    oc_gzip_t *g = (oc_gzip_t *)kmalloc(sizeof(oc_gzip_t));
    if (!g) return OC_GZIP_ERR_ARGS;
    oc_memset(g, 0, sizeof(*g));
    int rc = oc_gzip_init(g, one_shot_sink, &ctx);
    if (rc != 0) { kfree(g); return rc; }
    rc = oc_gzip_feed(g, in, in_len);
    if (rc == 0) rc = oc_gzip_finish(g);
    if (rc != 0) { kfree(g); return rc; }
    int n = ctx.len;
    kfree(g);
    return n;
}
