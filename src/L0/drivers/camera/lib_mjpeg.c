/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/camera/lib_mjpeg.c
 *
 * Baseline JPEG decoder.  See lib_mjpeg.h for the contract.  The
 * implementation follows ITU T.81:
 *
 *   1. syntax walk (markers)          -> jpeg_parse()
 *   2. Huffman entropy decode per MCU -> jpeg_scan()
 *   3. dequantize + integer IDCT      -> idct_block() (32-bit coef path,
 *      so DC*Q products up to the T.81 precision limits never truncate)
 *   4. chroma upsample + color        -> ycbcr_to_rgb() BT.601 integer
 *
 * Everything is bounds-checked; no recursion; the only heap use is the
 * per-component sample planes (freed again by lib_mjpeg_decode).
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "lib_mjpeg.h"

/* ------------------------------------------------------------------ */
/* error reporting                                                     */
/* ------------------------------------------------------------------ */

static const char *g_mjpeg_err = "ok";
const char *lib_mjpeg_last_error(void) { return g_mjpeg_err; }

#define FAIL(msg) do { g_mjpeg_err = (msg); return -1; } while (0)

/* ------------------------------------------------------------------ */
/* integer IDCT                                                        */
/* ------------------------------------------------------------------ */

/*
 * Q13 one-dimensional inverse-DCT matrix:
 *   M[j][k] = round(8192 * a(k) * cos((2j+1) * k * pi / 16))
 * with a(0) = 1/sqrt(2), a(k>0) = 1.
 * Two passes (rows, columns) with the implicit transpose give the 2-D
 * IDCT (input Q26 overall).  tests/host_camera_test.c validates this
 * matrix against a double-precision reference (max abs error <= 1 over
 * the full 8x8 basis and against real JPEG fixtures).
 */
static const i16 g_mjpeg_idct_m[8][8] = {
    { 5793, 8035, 7568, 6811, 5793, 4552, 3135, 1598 },
    { 5793, 6811, 3135,-1598,-5793,-8035,-7568,-4552 },
    { 5793, 4552,-3135,-8035,-5793, 1598, 7568, 6811 },
    { 5793, 1598,-7568,-4552, 5793, 6811,-3135,-8035 },
    { 5793,-1598,-7568, 4552, 5793,-6811,-3135, 8035 },
    { 5793,-4552,-3135, 8035,-5793,-1598, 7568,-6811 },
    { 5793,-6811, 3135, 1598,-5793, 8035,-7568, 4552 },
    { 5793,-8035, 7568,-6811, 5793,-4552, 3135,-1598 },
};

/* One 8x8 block: frequency-domain coefficients (already dequantized)
 * -> spatial samples 0..255. */
static void idct_block(const i32 *coef, u8 *out, u32 out_stride)
{
    i32 tmp[8][8];
    int i, j, k;

    /* rows: tmp[i][j] = sum_k M[i][k] * coef[j][k] >> 13 */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            i64 acc = 0;
            for (k = 0; k < 8; k++)
                acc += (i64)g_mjpeg_idct_m[i][k] * coef[j * 8 + k];
            tmp[i][j] = (i32)((acc + 4096) >> 13);   /* rounded */
        }
    }
    /* columns + level shift + clamp.  The row pass produced
     * tmp[x][v] = 2*g(x,v) (partial IDCT along the horizontal axis),
     * so the second pass sums over v for a fixed x - i.e. it reads
     * tmp[j][k] with j = x (spatial) and k = v (frequency), NOT
     * tmp[k][j].  The extra >>2 implements the 1/4 overall factor of
     * the 2-D inverse DCT ((1/2) per 1-D pass). */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            i64 acc = 0;
            for (k = 0; k < 8; k++)
                acc += (i64)g_mjpeg_idct_m[i][k] * tmp[j][k];
            i32 v = (i32)((acc + 16384) >> 15) + 128;   /* rounded */
            if (v < 0)   v = 0;
            if (v > 255) v = 255;
            out[i * out_stride + j] = (u8)v;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Huffman decoding                                                    */
/* ------------------------------------------------------------------ */

#define MJPEG_MAX_COMP   4

typedef struct mjpeg_huff {
    u8  bits[17];        /* bits[l] = #codes of length l (l=1..16)   */
    u8  vals[256];       /* symbol values in code order              */
    int mincode[17];     /* first code of length l                   */
    int maxcode[17];     /* one past last code of length l           */
    int valptr[17];      /* index into vals[] of first length-l code */
} mjpeg_huff_t;

/* ------------------------------------------------------------------ */
/* bit reader                                                          */
/* ------------------------------------------------------------------ */

typedef struct mjpeg_bitrd {
    const u8 *buf;
    u32  len;
    u32  pos;            /* next byte to load                        */
    u32  bitbuf;         /* MSB-aligned bits, bitcnt valid bits      */
    int  bitcnt;
    u32  consumed;       /* total bits dropped (for byte alignment)  */
    int  marker_seen;    /* a non-RST marker terminated the scan     */
    int  pending_rst;    /* RSTn sits at buf[pos..pos+1], bit-aligned */
} mjpeg_bitrd_t;

/* Fill the bit buffer.  Invariant: the valid bits occupy the TOP
 * bitcnt bits of bitbuf (bitbuf << (32-bitcnt) drops nothing).  A new
 * byte therefore goes to bit (24 - bitcnt) - placing it with a shift,
 * NOT with a plain (bitbuf << 8) | b, which only works when the buffer
 * is refilled from empty and silently corrupts the alignment when a
 * refill happens mid-symbol (caught by the 4:4:4 fixture: the third
 * component's DC code read zeros).  Stops cleanly at markers:
 *   FF 00      -> stuffed FF, loaded as data
 *   FF D0..D7  -> RSTn: pending_rst set, not loaded past it
 *   FF other   -> marker_seen set (scan ends; peek pads 1 bits)
 * Returns 1 when no more data bits can be loaded (1-padding aside). */
static int bitrd_fill(mjpeg_bitrd_t *br)
{
    for (;;) {
        if (br->bitcnt > 24) return 0;
        if (br->pos >= br->len) return 1;
        u8 b = br->buf[br->pos];
        if (b == 0xFF) {
            if (br->pos + 1 >= br->len) {      /* truncated FF */
                br->marker_seen = 1;
                return 1;
            }
            u8 nx = br->buf[br->pos + 1];
            if (nx == 0x00) {                  /* stuffed FF  */
                br->pos += 2;
                br->bitbuf |= 0xFFu << (24 - br->bitcnt);
                br->bitcnt += 8;
                continue;
            }
            if (nx >= 0xD0 && nx <= 0xD7) {    /* RSTn        */
                br->pending_rst = 1;
                return br->bitcnt <= 0;
            }
            br->marker_seen = 1;               /* real marker */
            return br->bitcnt <= 0;
        }
        br->pos++;
        br->bitbuf |= (u32)b << (24 - br->bitcnt);
        br->bitcnt += 8;
    }
}

static u32 bitrd_peek(mjpeg_bitrd_t *br, int n)
{
    if (br->bitcnt < n && !br->marker_seen && !br->pending_rst)
        bitrd_fill(br);
    if (n <= 0) return 0;
    if (br->bitcnt >= n)
        return (br->bitbuf >> (32 - n)) & ((1u << n) - 1);
    /* data ended: pad with 1 bits (JPEG EOB padding) */
    u32 v = (br->bitcnt > 0) ? (br->bitbuf >> (32 - br->bitcnt)) &
                              ((1u << br->bitcnt) - 1) : 0;
    v <<= (n - br->bitcnt);
    v |= (1u << (n - br->bitcnt)) - 1;
    return v;
}

static void bitrd_drop(mjpeg_bitrd_t *br, int n)
{
    if (n > br->bitcnt) n = br->bitcnt;
    if (n <= 0) return;
    br->consumed += (u32)n;
    br->bitbuf <<= n;
    br->bitcnt  -= n;
}

/* Receive one extended (ssss) value, ITU T.81 F.2.2.1 EXTEND. */
static int bitrd_extend(mjpeg_bitrd_t *br, int s)
{
    if (s == 0) return 0;
    int v = (int)bitrd_peek(br, s);
    bitrd_drop(br, s);
    if (v < (1 << (s - 1)))
        v += (-(1 << s)) + 1;
    return v;
}

/* Decode one Huffman symbol (T.81 F.2.2.3).  Returns the symbol or
 * -1 when the code is invalid / data ended.  NOTE: a complete code
 * that matches at length l is returned even when the bit reader hit
 * the terminating marker while loading PADDING for a later peek - the
 * marker only ends the NEXT symbol (T.81 B.1.1.5: the scan ends after
 * the last code, and fill() loads ahead). */
static int huff_decode(mjpeg_bitrd_t *br, const mjpeg_huff_t *h)
{
    int code = 0, l;
    /* No marker check inside the loop: a marker seen by a look-ahead
     * fill() does NOT invalidate the bits already in the buffer, and a
     * restart marker is byte-aligned so every code of the current
     * interval is fully buffered.  A code that matches wins; if no
     * length matches at all the code is invalid (-1).  The scan loop
     * itself stops on marker_seen. */
    for (l = 1; l <= 16; l++) {
        code = (code << 1) | (int)bitrd_peek(br, 1);
        bitrd_drop(br, 1);
        if (code - h->mincode[l] < h->bits[l])
            return h->vals[h->valptr[l] + (code - h->mincode[l])];
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* decode state                                                        */
/* ------------------------------------------------------------------ */

typedef struct mjpeg_comp {
    int id;
    int h, v;            /* sampling factors                         */
    int tq;              /* quant table index                        */
    int tdc, tac;        /* Huffman table indices                    */
    int dcpred;
    /* per-frame geometry (component plane, padded to block multiples) */
    int  blocks_w, blocks_h;
    u8  *plane;          /* full-resolution 8-bit plane              */
    int  plane_stride;
} mjpeg_comp_t;

typedef struct mjpeg_dec {
    /* frame */
    int  w, h;
    int  ncomp;
    int  precision;
    int  restart_interval;
    /* tables */
    u16  qt[4][64];      /* natural (raster) order                   */
    int  qt_seen[4];
    mjpeg_huff_t huff[2][4];
    /* components */
    mjpeg_comp_t comp[MJPEG_MAX_COMP];
    int  mcu_w, mcu_h;               /* in pixels                    */
    int  mcus_x, mcus_y;             /* MCU grid                     */
    int  blocks_per_mcu;
    /* scan (SOS) */
    int  scan_ncomp;
    int  scan_comp[MJPEG_MAX_COMP];  /* component order in this scan */
} mjpeg_dec_t;

/* zig-zag order (T.81 A.6) */
static const u8 g_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* ------------------------------------------------------------------ */
/* scan (entropy-coded data)                                           */
/* ------------------------------------------------------------------ */

static void jpeg_free_planes(mjpeg_dec_t *d)
{
    int c;
    for (c = 0; c < MJPEG_MAX_COMP; c++) {
        if (d->comp[c].plane) { kfree(d->comp[c].plane); d->comp[c].plane = 0; }
    }
}

/* Run one SOS's entropy-coded data.  Returns 0 when the scan ended
 * (marker / data end), -1 on corruption.  The SOS header itself was
 * parsed by the caller; br starts at the first entropy byte. */
static int jpeg_scan(mjpeg_dec_t *d, mjpeg_bitrd_t *br)
{
    i32 block[64];
    int  it_x = 0, it_y = 0;
    int  rst_count = d->restart_interval;
    int  i;

    /* Iteration domain (T.81 A.2.3): a multi-component scan walks the
     * MCU grid; a single-component scan walks the component's own
     * block grid linearly ("MCU" = one data unit there). */
    int iters_x, iters_y;
    if (d->scan_ncomp == 1) {
        mjpeg_comp_t *cp0 = &d->comp[d->scan_comp[0]];
        iters_x = cp0->blocks_w;
        iters_y = cp0->blocks_h;
    } else {
        iters_x = d->mcus_x;
        iters_y = d->mcus_y;
    }

    for (;;) {
        if (br->marker_seen) break;
        if (br->pos >= br->len) break;

        /* ---- decode one iteration (MCU or single block) ---- */
        int bi;
        for (bi = 0; bi < d->blocks_per_mcu; bi++) {
            /* which component owns this block */
            int comp = 0, bi2 = bi;
            for (i = 0; i < d->scan_ncomp; i++) {
                mjpeg_comp_t *cp = &d->comp[d->scan_comp[i]];
                int nb = cp->h * cp->v;
                if (bi2 < nb) { comp = d->scan_comp[i]; break; }
                bi2 -= nb;
            }
            mjpeg_comp_t *cp = &d->comp[comp];
            const mjpeg_huff_t *hdc = &d->huff[0][cp->tdc];
            const mjpeg_huff_t *hac = &d->huff[1][cp->tac];
            const u16 *qt = d->qt[cp->tq];

            int s = huff_decode(br, hdc);
            if (s < 0) FAIL("bad DC huffman code");
            int diff = bitrd_extend(br, s);
            cp->dcpred += diff;
            block[0] = cp->dcpred * (i32)qt[0];   /* dequant DC */

            int k = 1, run;
            while (k < 64) {
                int rs = huff_decode(br, hac);
                if (rs < 0) FAIL("bad AC huffman code");
                run = rs >> 4;
                s   = rs & 15;
                if (s == 0) {
                    if (run == 15) { k += 16; continue; }   /* ZRL */
                    break;                                   /* EOB */
                }
                k += run;
                if (k > 63) FAIL("AC coefficient overflows block");
                /* dequant AC: zigzag slot k lives at natural index
                 * g_zigzag[k] in both the block and the quant table */
                block[g_zigzag[k]] = bitrd_extend(br, s) * (i32)qt[g_zigzag[k]];
                k++;
            }
            for (; k < 64; k++) block[g_zigzag[k]] = 0;

            /* IDCT into the component plane at the block position */
            int bx, by;
            if (d->scan_ncomp == 1) {
                bx = it_x;                       /* linear block order */
                by = it_y;
            } else {
                bx = it_x * cp->h + (bi2 % cp->h);
                by = it_y * cp->v + (bi2 / cp->h);
            }
            if (bx < cp->blocks_w && by < cp->blocks_h) {
                u8 *dst = cp->plane + (u32)by * 8 * cp->plane_stride
                                   + (u32)bx * 8;
                idct_block(block, dst, cp->plane_stride);
            }
        }

        /* ---- advance the iteration grid ---- */
        if (++it_x >= iters_x) {
            it_x = 0;
            if (++it_y >= iters_y) break;         /* scan complete */
        }

        /* ---- restart interval ---- */
        if (d->restart_interval && --rst_count == 0) {
            /* byte-align, then consume the pending RSTn marker */
            bitrd_drop(br, (int)((8 - (br->consumed & 7)) & 7));
            if (br->pending_rst) {
                br->pending_rst = 0;
                br->pos += 2;                    /* skip FF Dn */
            } else if (!br->marker_seen && br->pos + 1 < br->len &&
                       br->buf[br->pos] == 0xFF &&
                       br->buf[br->pos + 1] >= 0xD0 &&
                       br->buf[br->pos + 1] <= 0xD7) {
                br->pos += 2;
            } else {
                break;    /* restart expected but missing: scan done */
            }
            int c;
            for (c = 0; c < MJPEG_MAX_COMP; c++)
                d->comp[c].dcpred = 0;
            rst_count = d->restart_interval;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* syntax walker                                                       */
/* ------------------------------------------------------------------ */

static int jpeg_parse(mjpeg_dec_t *d, const u8 *buf, u32 len)
{
    memset(d, 0, sizeof(*d));
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8)
        FAIL("missing SOI");

    u32 pos = 2;
    int  seen_sof = 0;
    int  hmax = 1, vmax = 1;
    int  c, i;

    while (pos + 1 < len) {
        if (buf[pos] != 0xFF) { pos++; continue; }
        u8 marker = buf[pos + 1];
        if (marker == 0xFF) { pos++; continue; }
        pos += 2;
        if (marker == 0xD8) continue;                   /* SOI (again) */
        if (marker == 0xD9) break;                      /* EOI         */
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;                                   /* standalone  */

        if (pos + 2 > len) FAIL("marker without length");
        u16 seglen = (u16)((buf[pos] << 8) | buf[pos + 1]);
        if (seglen < 2 || pos + seglen > len) FAIL("bad segment length");
        const u8 *seg = buf + pos + 2;
        u32 segdata = seglen - 2;

        switch (marker) {
        case 0xC0: case 0xC1: {                         /* SOF0/SOF1  */
            if (segdata < 6) FAIL("SOF too short");
            d->precision = seg[0];
            if (d->precision != 8) FAIL("only 8-bit precision supported");
            d->h = (seg[1] << 8) | seg[2];
            d->w = (seg[3] << 8) | seg[4];
            d->ncomp = seg[5];
            if (d->w <= 0 || d->h <= 0) FAIL("bad image size");
            if (d->w > 4096 || d->h > 4096)
                FAIL("image too large for kernel decoder");
            if (d->ncomp < 1 || d->ncomp > 3) FAIL("unsupported component count");
            if (segdata < 6 + 3 * (u32)d->ncomp) FAIL("SOF component data missing");
            hmax = 1; vmax = 1;
            for (c = 0; c < d->ncomp; c++) {
                const u8 *cs = seg + 6 + 3 * c;
                d->comp[c].id = cs[0];
                d->comp[c].h  = cs[1] >> 4;
                d->comp[c].v  = cs[1] & 15;
                d->comp[c].tq = cs[2];
                if (d->comp[c].h < 1 || d->comp[c].h > 4 ||
                    d->comp[c].v < 1 || d->comp[c].v > 4)
                    FAIL("bad sampling factors");
                if (d->comp[c].tq > 3) FAIL("bad quant table index");
                if (d->comp[c].h > hmax) hmax = d->comp[c].h;
                if (d->comp[c].v > vmax) vmax = d->comp[c].v;
            }
            seen_sof = 1;
            break;
        }
        case 0xC2:                                      /* progressive */
            FAIL("progressive JPEG not supported (baseline only)");
        case 0xC3: case 0xC5: case 0xC6: case 0xC7:
        case 0xC9: case 0xCA: case 0xCB:
        case 0xCD: case 0xCE: case 0xCF:
            FAIL("lossless/arithmetic JPEG not supported");
        case 0xC4: {                                    /* DHT         */
            u32 off = 0;
            while (off + 17 <= segdata) {
                const u8 *th = seg + off;
                int tc = th[0] >> 4, t = th[0] & 15;
                if (tc > 1 || t > 3) FAIL("bad DHT class/index");
                mjpeg_huff_t *h = &d->huff[tc][t];
                int total = 0, l;
                for (l = 1; l <= 16; l++) {
                    h->bits[l] = th[l];
                    total += h->bits[l];
                }
                if (total < 1 || total > 256 || off + 17 + (u32)total > segdata)
                    FAIL("bad DHT count");
                memcpy(h->vals, th + 17, total);
                /* canonical code build (T.81 F.2.2.3) */
                int code = 0, k = 0;
                for (l = 1; l <= 16; l++) {
                    h->valptr[l] = k;
                    h->mincode[l] = code;
                    code += h->bits[l];
                    k    += h->bits[l];
                    h->maxcode[l] = code;
                    code <<= 1;
                }
                if (k != total) FAIL("DHT bits/values mismatch");
                off += 17 + (u32)total;
            }
            break;
        }
        case 0xDB: {                                    /* DQT         */
            u32 off = 0;
            while (off < segdata) {
                u8 pq = seg[off] >> 4, t = seg[off] & 15;
                if (t > 3) FAIL("bad DQT index");
                if (pq) {
                    if (off + 1 + 128 > segdata) FAIL("DQT 16-bit truncated");
                    for (c = 0; c < 64; c++)
                        d->qt[t][g_zigzag[c]] =
                            (u16)((seg[off + 1 + 2*c] << 8) | seg[off + 2 + 2*c]);
                    off += 129;
                } else {
                    if (off + 1 + 64 > segdata) FAIL("DQT 8-bit truncated");
                    for (c = 0; c < 64; c++)
                        d->qt[t][g_zigzag[c]] = seg[off + 1 + c];
                    off += 65;
                }
                d->qt_seen[t] = 1;
            }
            break;
        }
        case 0xDD:                                      /* DRI         */
            if (segdata < 2) FAIL("DRI too short");
            d->restart_interval = (seg[0] << 8) | seg[1];
            break;
        case 0xDA: {                                    /* SOS         */
            if (!seen_sof) FAIL("SOS before SOF");
            if (segdata < 1) FAIL("SOS too short");
            int ns = seg[0];
            if (ns < 1 || ns > 4 || segdata < 1 + 2 * (u32)ns + 3)
                FAIL("bad SOS header");
            d->scan_ncomp = ns;
            for (i = 0; i < ns; i++) {
                int cs = seg[1 + 2 * i];
                int td = seg[2 + 2 * i] >> 4;
                int ta = seg[2 + 2 * i] & 15;
                int found = -1;
                for (c = 0; c < d->ncomp; c++)
                    if (d->comp[c].id == cs) { found = c; break; }
                if (found < 0 || td > 3 || ta > 3)
                    FAIL("SOS references unknown component/table");
                d->scan_comp[i] = found;
                d->comp[found].tdc = td;
                d->comp[found].tac = ta;
            }
            /* Ss, Se, Ah/Al = seg[1+2*ns .. +3]: baseline fixed values
             * (0,63,0), not used further by a baseline decoder.      */

            for (c = 0; c < d->ncomp; c++)
                if (!d->qt_seen[d->comp[c].tq]) FAIL("missing quant table");

            /* geometry + component planes */
            d->mcu_w = hmax * 8;
            d->mcu_h = vmax * 8;
            d->mcus_x = (d->w + d->mcu_w - 1) / d->mcu_w;
            d->mcus_y = (d->h + d->mcu_h - 1) / d->mcu_h;
            d->blocks_per_mcu = 0;
            for (c = 0; c < d->ncomp; c++) {
                mjpeg_comp_t *cp = &d->comp[c];
                cp->blocks_w = (d->w * cp->h + hmax * 8 - 1) / (hmax * 8);
                cp->blocks_h = (d->h * cp->v + vmax * 8 - 1) / (vmax * 8);
                cp->plane_stride = cp->blocks_w * 8;
                if (!cp->plane) {
                    u32 psz = (u32)cp->plane_stride * (u32)(cp->blocks_h * 8);
                    cp->plane = (u8 *)kmalloc(psz ? psz : 1);
                    if (!cp->plane) {
                        jpeg_free_planes(d);
                        FAIL("out of memory for sample planes");
                    }
                    memset(cp->plane, 128, psz);
                }
                d->blocks_per_mcu += cp->h * cp->v;
            }

            /* run the scan; the bit reader starts right after SOS */
            mjpeg_bitrd_t br;
            memset(&br, 0, sizeof(br));
            br.buf = buf;
            br.len = len;
            br.pos = pos + seglen;
            int rc = jpeg_scan(d, &br);
            if (rc < 0) return -1;
            pos = br.pos;
            break;
        }
        default:
            break;                     /* APPn/COM - skipped */
        }
        pos += seglen;
    }
    if (!seen_sof) FAIL("no SOF found");
    return 0;
}

/* ------------------------------------------------------------------ */
/* color conversion                                                    */
/* ------------------------------------------------------------------ */

static inline u8 clamp8(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (u8)v;
}

/* BT.601 (libjpeg-style fixed point) */
static inline void ycbcr_to_rgb(int y, int cb, int cr,
                                u8 *r, u8 *g, u8 *b)
{
    cb -= 128; cr -= 128;
    *r = clamp8(y                    + ((91881 * cr) >> 16));
    *g = clamp8(y - ((22554 * cb + 46802 * cr) >> 16));
    *b = clamp8(y + ((116130 * cb) >> 16));
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int lib_mjpeg_decode(const u8 *jpeg, u32 len, u8 *out_rgb, u32 out_max,
                     u32 *out_w, u32 *out_h, u32 *out_stride)
{
    g_mjpeg_err = "ok";
    if (!jpeg || len < 4) { g_mjpeg_err = "no data"; return -1; }

    mjpeg_dec_t dec;
    memset(&dec, 0, sizeof(dec));
    int rc = jpeg_parse(&dec, jpeg, len);
    if (rc < 0) { jpeg_free_planes(&dec); return -1; }

    u32 need = (u32)dec.w * 3u * (u32)dec.h;
    if (out_stride) *out_stride = (u32)dec.w * 3u;
    if (out_w) *out_w = (u32)dec.w;
    if (out_h) *out_h = (u32)dec.h;
    if (need > out_max) {
        g_mjpeg_err = "output buffer too small";
        jpeg_free_planes(&dec);
        return -2;
    }

    /* largest sampling factors (for the subsample map) */
    int hmax = 1, vmax = 1, c;
    for (c = 0; c < dec.ncomp; c++) {
        if (dec.comp[c].h > hmax) hmax = dec.comp[c].h;
        if (dec.comp[c].v > vmax) vmax = dec.comp[c].v;
    }

    /* pack the (possibly subsampled) component planes into RGB:
     * plane pixel = image pixel mapped by the component's sampling
     * factors (nearest-neighbour upsample). */
    int x, y;
    for (y = 0; y < dec.h; y++) {
        u8 *row = out_rgb + (u32)y * 3u * (u32)dec.w;
        int py0 = y * dec.comp[0].v / vmax;
        int py1 = (dec.ncomp > 1) ? y * dec.comp[1].v / vmax : 0;
        int py2 = (dec.ncomp > 2) ? y * dec.comp[2].v / vmax : 0;
        for (x = 0; x < dec.w; x++) {
            int yv = dec.comp[0].plane[py0 * dec.comp[0].plane_stride
                                       + x * dec.comp[0].h / hmax];
            if (dec.ncomp < 3) {                 /* grayscale */
                row[x*3+0] = row[x*3+1] = row[x*3+2] = (u8)yv;
                continue;
            }
            int cbv = dec.comp[1].plane[py1 * dec.comp[1].plane_stride
                                        + x * dec.comp[1].h / hmax];
            int crv = dec.comp[2].plane[py2 * dec.comp[2].plane_stride
                                        + x * dec.comp[2].h / hmax];
            u8 r, g, b;
            ycbcr_to_rgb(yv, cbv, crv, &r, &g, &b);
            row[x*3+0] = r; row[x*3+1] = g; row[x*3+2] = b;
        }
    }
    jpeg_free_planes(&dec);
    return 0;
}
