/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/gzip.h
 * Purpose: gzip (RFC 1952) container + DEFLATE (RFC 1951) inflate engine.
 *
 * WP-10u ships system updates as *.tar.gz packages, so the kernel needs a
 * real decompressor.  This is a complete, self-contained inflate: stored,
 * fixed-Huffman and dynamic-Huffman blocks, resumable across feed()
 * chunk boundaries, with CRC32 + ISIZE verification of the gzip trailer.
 *
 * Streaming model:
 *
 *     lib_gzip_t *g = kmalloc(sizeof(lib_gzip_t));   (about 33 KiB, heap only)
 *     memset(g, 0, sizeof(*g));
 *     lib_gzip_init(g, my_sink, my_ctx);
 *     while ((n = read(chunk)) > 0) lib_gzip_feed(g, chunk, n);
 *     int rc = lib_gzip_finish(g);   (0 = stream complete and verified)
 *
 * Decompressed bytes are handed to the sink in chunks (<= 512 bytes per
 * call).  A sink returning non-zero aborts with OC_GZIP_ERR_SINK.
 *
 * All error codes are numeric (see below); no text inside the engine.
 */
#ifndef OC_GZIP_H
#define OC_GZIP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (all negative). */
#define OC_GZIP_ERR_NONE     0
#define OC_GZIP_ERR_ARGS   (-1)   /* NULL pointer / bad capacity           */
#define OC_GZIP_ERR_MAGIC  (-2)   /* not a gzip stream (1f 8b)             */
#define OC_GZIP_ERR_METHOD (-3)   /* CM != 8 (deflate)                     */
#define OC_GZIP_ERR_HEADER (-4)   /* malformed gzip header                 */
#define OC_GZIP_ERR_BLOCK  (-5)   /* invalid block header / length check   */
#define OC_GZIP_ERR_TABLE  (-6)   /* invalid Huffman table                 */
#define OC_GZIP_ERR_DIST   (-7)   /* distance beyond window                */
#define OC_GZIP_ERR_TRUNC  (-8)   /* stream ended before final block       */
#define OC_GZIP_ERR_CRC    (-9)   /* CRC32 mismatch in gzip trailer        */
#define OC_GZIP_ERR_ISIZE  (-10)  /* ISIZE mismatch in gzip trailer        */
#define OC_GZIP_ERR_SINK   (-11)  /* sink callback failed                  */
#define OC_GZIP_ERR_STATE  (-12)  /* API misuse (feed after finish, ...)   */

/* Chunked-decompression sink: return 0 to continue, non-zero to abort. */
typedef int (*lib_gzip_sink_fn)(void *ctx, const u8 *data, int len);

/* Decompressor state (~33 KiB, always heap-allocate + zero it). */
typedef struct {
    lib_gzip_sink_fn sink;          /* decompressed-data callback     */
    void          *sink_ctx;       /* user context for the callback  */

    /* bit reader (resumable across feed() calls) */
    u32  bitbuf;
    int  bitcnt;

    /* current input chunk */
    const u8 *in;
    int       in_len;
    int       in_pos;

    /* inflate state machine */
    int  state;                    /* OC_GZS_* */
    int  last_block;
    int  btype;

    /* multi-bit accumulator (read_bits resumes through d_acc+acc_bits) */
    u32  d_acc;
    int  acc_bits;

    /* stored block */
    u32  stored_left;              /* remaining stored data bytes     */
    int  stored_len_read;          /* LEN/NLEN bytes already consumed */
    u32  stored_len_acc;           /* LEN accumulator                 */
    u32  stored_nlen_acc;          /* NLEN accumulator                */

    /* huffman tables */
    short counts_lit[16];
    short syms_lit[288];
    short counts_dst[16];
    short syms_dst[32];
    int   tables_kind;             /* 0=none 1=fixed 2=dynamic        */

    /* dynamic table construction state (resumable) */
    int   hlit, hdist, hclen;
    short lens_lit[288];
    short lens_dst[32];
    short lens_cl[19];
    short cl_syms[19];
    int   t_idx;                   /* symbol index being filled       */
    int   t_phase;                 /* TP_SINGLE / TP_REPEAT           */
    int   t_repeat_left;           /* remaining repeat count          */
    int   t_repeat_val;            /* value being repeated            */

    /* huffman decode resume state (per-symbol) */
    int   hd_len;
    int   hd_code;
    int   hd_first;
    int   hd_index;

    /* gzip header sub-state */
    int   h_sub;                   /* GH_* step counter               */
    int   h_flg;                   /* gzip FLG byte                   */
    u32   h_xlen;                  /* FEXTRA remaining length         */
    int   h_cnt;                   /* generic per-field byte counter  */

    /* decode sub-state (length/distance decoding, resumable) */
    int   d_phase;                 /* DP_* */
    u32   d_len;
    u32   d_dist;
    int   d_extra_left;

    /* LZ77 window */
    u8    window[32768];
    int   wpos;
    int   window_valid;            /* bytes ever written (max 32768)  */

    /* output staging */
    u8    out_buf[512];
    int   out_len;

    /* trailer + counters */
    u32   crc;                     /* running CRC (pre-final-xor)     */
    u32   out_bytes;
    int   trailer_left;            /* bytes read of 8-byte trailer    */
    u8    trailer[8];
    int   done;
    int   err;
} lib_gzip_t;

/* Prepare a decompressor.  Returns 0 on success, OC_GZIP_ERR_ARGS on
 * bad arguments.  g must be zeroed before the call (kmalloc + memset). */
int lib_gzip_init(lib_gzip_t *g, lib_gzip_sink_fn sink, void *sink_ctx);

/* Feed the next chunk of compressed bytes.  Returns 0 when more input is
 * expected (or the stream just finished), <0 on error.  Feeding after a
 * completed stream is a no-op (returns 0). */
int lib_gzip_feed(lib_gzip_t *g, const u8 *data, int len);

/* Verify the trailer: 0 = stream complete + CRC32/ISIZE verified.
 * OC_GZIP_ERR_TRUNC when the stream never reached its final block. */
int lib_gzip_finish(lib_gzip_t *g);

/* Convenience one-shot gunzip: writes at most out_cap bytes to out,
 * returns the decompressed length (>=0) or an OC_GZIP_ERR_* code.
 * Internally heap-allocates the ~33 KiB engine state. */
int gunzip(const u8 *in, int in_len, u8 *out, int out_cap);

/* CRC32 (gzip polynomial 0xEDB88320) over a buffer. */
u32 crc32(const void *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* OC_GZIP_H */
