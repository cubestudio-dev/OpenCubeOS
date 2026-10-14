/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-camera_misc
 *
 * File: tests/host_camera_test.c
 *
 * Host-side byte-level test of the camera subsystem.  The SAME kernel
 * .c files are compiled for the host so the exact production code
 * paths run here:
 *
 *   drivers/camera/lib_mjpeg.c          baseline JPEG decoder
 *   drivers/camera/driver_camera.c      camera core registry + 19 APIs
 *   drivers/camera/driver_camera_uvc.c  UVC class driver
 *
 * The USB transport is a scripted in-process fake (the same idea as
 * tools/bt_test_peer.py for Bluetooth): control transfers are answered
 * from a script table, bulk transfers replay pre-built UVC payload
 * packets.  The UVC descriptor fixture and the JPEG test vectors are
 * byte-exact per the UVC 1.1 / ITU T.81 specs; the JPEG vectors are
 * built by a tiny in-test encoder (DC-only blocks, standard tables).
 *
 * Build:
 *   gcc -O2 -g -Ikernel -Ikernel/lib -Ikernel/mem -Ikernel/core \
 *       -Idrivers/camera -Idrivers/usb -Idrivers/pci \
 *       tests/host_camera_test.c -o /tmp/host_camera_test -lm
 * (no <string.h>: lib_string.c below provides memset/memcpy/...)
 *
 * Exit code = number of failing checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* --- kernel service shims (freestanding code compiled for host) ------ */
#include "../kernel/types.h"

void *kmalloc(unsigned long long size) { return malloc(size); }
void  kfree(void *p)                   { free(p); }
u64   core_timer_now_ms(void)          { return 1234; }

#include "../kernel/lib/lib_string.c"
#include "../drivers/camera/lib_mjpeg.c"
#include "../drivers/camera/driver_camera.c"
#include "../drivers/camera/driver_camera_uvc.c"

static int g_check_id;
static int check(const char *name, int cond)
{
    g_check_id++;
    if (!cond) printf("[%d] ", g_check_id);
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* ================================================================== */
/* 1. integer IDCT vs double reference                                 */
/* ================================================================== */

static double idct_ref(const i32 *coef, int x, int y)
{
    double v = 0;
    for (int u = 0; u < 8; u++)
        for (int vv = 0; vv < 8; vv++) {
            double cu = (u == 0) ? (1.0 / sqrt(2.0)) : 1.0;
            double cv = (vv == 0) ? (1.0 / sqrt(2.0)) : 1.0;
            v += cu * cv * (double)coef[u * 8 + vv] *
                 cos((2 * x + 1) * u * 3.14159265358979 / 16.0) *
                 cos((2 * y + 1) * vv * 3.14159265358979 / 16.0);
        }
    return v / 4.0 + 128.0;
}

static int test_idct(void)
{
    int fails = 0;
    double maxerr = 0;
    srand(42);
    for (int trial = 0; trial < 16; trial++) {
        i32 coef[64];
        u8 out[64];
        for (int i = 0; i < 64; i++)
            coef[i] = (rand() % 4096) - 2048;   /* realistic 8-bit range */
        idct_block(coef, out, 8);
        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++) {
                /* the production block clamps to [0,255]; compare
                 * against the clamped reference */
                double ref = idct_ref(coef, x, y);
                if (ref < 0)   ref = 0;
                if (ref > 255) ref = 255;
                double err = ref - (double)out[x * 8 + y];
                if (err < 0) err = -err;
                if (err > maxerr) maxerr = err;
            }
    }
    fails += check("integer IDCT vs double (maxerr<=1)", maxerr <= 1.0);
    printf("  max abs error: %.3f\n", maxerr);
    return fails;
}

/* ================================================================== */
/* 2. JPEG test-vector builder (DC-only baseline encoder)              */
/* ================================================================== */

typedef struct {
    u8  buf[65536];
    u32 n;
    u32 bits;       /* bit accumulator (MSB first) */
    int nbits;
} jw_t;             /* JPEG writer */

static void jw_byte(jw_t *w, u8 b)
{
    w->buf[w->n++] = b;
}

static void jw_word(jw_t *w, u16 v)
{
    jw_byte(w, (u8)(v >> 8));
    jw_byte(w, (u8)(v & 0xFF));
}

static void jw_mark(jw_t *w, u8 marker)
{
    jw_byte(w, 0xFF);
    jw_byte(w, marker);
}

static void jw_flush(jw_t *w)
{
    if (w->nbits) {
        u8 b = (u8)(w->bits << (8 - w->nbits));
        jw_byte(w, b);
        if (b == 0xFF) jw_byte(w, 0x00);   /* byte stuffing */
        w->bits = 0;
        w->nbits = 0;
    }
}

static void jw_put(jw_t *w, u32 val, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--) {
        w->bits = (w->bits << 1) | ((val >> i) & 1);
        w->nbits++;
        if (w->nbits == 8) {
            u8 b = (u8)w->bits;
            jw_byte(w, b);
            if (b == 0xFF) jw_byte(w, 0x00);   /* byte stuffing */
            w->bits = 0;
            w->nbits = 0;
        }
    }
}

/* DC huffman: 4-bit codes, symbol = magnitude class (0..15)  */
/* AC huffman: 1-bit code, symbol = EOB (0x00)                 */

static void jw_dqt(jw_t *w)
{
    jw_mark(w, 0xDB);
    jw_word(w, 2 + 1 + 64);
    jw_byte(w, 0x00);                  /* Pq=0, Tq=0 */
    for (int i = 0; i < 64; i++) jw_byte(w, 1);
}

static void jw_dht(jw_t *w)
{
    /* DC table 0: 16 codes of length 4 (symbols 0..15) */
    jw_mark(w, 0xC4);
    jw_word(w, 2 + 1 + 16 + 16);
    jw_byte(w, 0x00);
    for (int i = 0; i < 16; i++)
        jw_byte(w, (i == 3) ? 16 : 0);
    for (int i = 0; i < 16; i++) jw_byte(w, (u8)i);
    /* AC table 0: one code of length 1 (EOB) */
    jw_mark(w, 0xC4);
    jw_word(w, 2 + 1 + 16 + 1);
    jw_byte(w, 0x10);
    for (int i = 0; i < 16; i++)
        jw_byte(w, (i == 0) ? 1 : 0);
    jw_byte(w, 0x00);
}

/* magnitude class */
static int mag_class(int v)
{
    int s = 0;
    int a = v < 0 ? -v : v;
    while (a) { s++; a >>= 1; }
    return s;
}

static void jw_dc_block(jw_t *w, int diff)
{
    int s = mag_class(diff);
    jw_put(w, (u32)s, 4);             /* DC code for class s (code word = s) */
    if (s) {
        u32 bits = (diff > 0) ? (u32)diff
                              : (u32)((diff - 1) & ((1 << s) - 1));
        jw_put(w, bits, s);
    }
    jw_put(w, 0, 1);                  /* AC EOB */
}

/* Build a baseline JPEG: DC-only blocks.
 * kind: 0 = gray WxH, 1 = 4:4:4, 2 = 4:2:0 (W,H multiples of 8/16)
 * yvals: per-Y-block value (cycled); cb, cr plane values. dri = DRI. */
static u32 build_jpeg(u8 *out, u32 cap, int kind, int w, int h,
                      const int *yvals, int cb, int cr, int dri)
{
    printf("  [bj enter kind=%d %dx%d]\n", kind, w, h); fflush(stdout);
    jw_t *jw = (jw_t *)malloc(sizeof(jw_t));
    jw->n = 0; jw->bits = 0; jw->nbits = 0;

    jw_mark(jw, 0xD8);                          /* SOI */

    if (dri) {                                 /* DRI */
        jw_mark(jw, 0xDD);
        jw_word(jw, 4);
        jw_word(jw, (u16)dri);
    }

    jw_dqt(jw);
    jw_dht(jw);

    /* SOF0 */
    int nc = (kind == 0) ? 1 : 3;
    jw_mark(jw, 0xC0);
    jw_word(jw, (u16)(8 + 3 * nc));
    jw_byte(jw, 8);
    jw_word(jw, (u16)h);
    jw_word(jw, (u16)w);
    jw_byte(jw, (u8)nc);
    if (kind == 0) {
        jw_byte(jw, 1); jw_byte(jw, 0x11); jw_byte(jw, 0);
    } else if (kind == 1) {
        jw_byte(jw, 1); jw_byte(jw, 0x11); jw_byte(jw, 0);
        jw_byte(jw, 2); jw_byte(jw, 0x11); jw_byte(jw, 0);
        jw_byte(jw, 3); jw_byte(jw, 0x11); jw_byte(jw, 0);
    } else {
        jw_byte(jw, 1); jw_byte(jw, 0x22); jw_byte(jw, 0);
        jw_byte(jw, 2); jw_byte(jw, 0x11); jw_byte(jw, 0);
        jw_byte(jw, 3); jw_byte(jw, 0x11); jw_byte(jw, 0);
    }

    /* SOS */
    jw_mark(jw, 0xDA);
    jw_word(jw, (u16)(6 + 2 * nc));
    jw_byte(jw, (u8)nc);
    if (kind == 0) {
        jw_byte(jw, 1); jw_byte(jw, 0x00);
    } else {
        jw_byte(jw, 1); jw_byte(jw, 0x00);
        jw_byte(jw, 2); jw_byte(jw, 0x00);
        jw_byte(jw, 3); jw_byte(jw, 0x00);
    }
    jw_byte(jw, 0); jw_byte(jw, 63); jw_byte(jw, 0);

    /* entropy-coded data */
    int predY = 0, predCb = 0, predCr = 0;

    if (kind == 0) {
        int nblk = (w / 8) * (h / 8);
        for (int b = 0; b < nblk; b++) {
            if (dri && b && b % dri == 0) {
                /* restart marker: byte-align, DC predictors reset */
                jw_flush(jw);
                jw_mark(jw, (u8)(0xD0 + ((b / dri - 1) & 7)));
                predY = 0;
            }
            int dc = 8 * (yvals[0] - 128);
            jw_dc_block(jw, dc - predY);
            predY = dc;
        }
    } else if (kind == 1) {
        int mcux = w / 8, mcuy = h / 8;
        for (int my = 0; my < mcuy; my++)
            for (int mx = 0; mx < mcux; mx++) {
                int bi = (my * mcux + mx) % 4;
                int dc = 8 * (yvals[bi] - 128);
                jw_dc_block(jw, dc - predY);
                predY = dc;
                int dcb = 8 * (cb - 128);
                jw_dc_block(jw, dcb - predCb);
                predCb = dcb;
                int dcr = 8 * (cr - 128);
                jw_dc_block(jw, dcr - predCr);
                predCr = dcr;
            }
    } else {                                    /* 4:2:0 */
        int mcux = w / 16, mcuy = h / 16;
        for (int my = 0; my < mcuy; my++)
            for (int mx = 0; mx < mcux; mx++) {
                for (int b = 0; b < 4; b++) {
                    int dc = 8 * (yvals[b] - 128);
                    jw_dc_block(jw, dc - predY);
                    predY = dc;
                }
                int dcb = 8 * (cb - 128);
                jw_dc_block(jw, dcb - predCb);
                predCb = dcb;
                int dcr = 8 * (cr - 128);
                jw_dc_block(jw, dcr - predCr);
                predCr = dcr;
            }
    }
    printf("  [bj pre-flush n=%u]\n", jw->n); fflush(stdout);
    jw_flush(jw);
    jw_mark(jw, 0xD9);                          /* EOI */

    u32 n = jw->n;
    printf("  [bj copy n=%u cap=%u]\n", n, cap); fflush(stdout);
    if (n > cap) n = cap;
    memcpy(out, jw->buf, n);
    printf("  [bj copied]\n"); fflush(stdout);
    free(jw);
    printf("  [bj freed]\n"); fflush(stdout);
    return n;
}

/* ================================================================== */
/* 3. JPEG decoder tests                                               */
/* ================================================================== */

static int test_jpeg_gray(void)
{
    printf("  [gray enter]\n"); fflush(stdout);
    u8 jpg[8192], rgb[64 * 64 * 3];
    int yv[1] = { 200 };
    u32 n = build_jpeg(jpg, sizeof(jpg), 0, 16, 16, yv, 0, 0, 0);
    printf("  [gray built n=%u]\n", n); fflush(stdout);
    if (check("gray jpeg built", n > 100) != 0) return 1;
    u32 w = 0, h = 0;
    printf("  [gray decode begin]\n"); fflush(stdout);
    int rc = lib_mjpeg_decode(jpg, n, rgb, sizeof(rgb), &w, &h, 0);
    printf("  [gray decode rc=%d]\n", rc); fflush(stdout);
    if (check("gray decode rc=0", rc == 0) != 0) {
        printf("  err: %s\n", lib_mjpeg_last_error());
        return 1;
    }
    check("gray size 16x16", w == 16 && h == 16);
    int bad = 0;
    for (u32 i = 0; i < w * h; i++)
        if (rgb[i*3] != 200 || rgb[i*3+1] != 200 || rgb[i*3+2] != 200) bad++;
    return check("gray 16x16 all pixels 200", bad == 0);
}

static int test_jpeg_444(void)
{
    u8 jpg[8192], rgb[32 * 32 * 3];
    /* 8x8 4:4:4: single Y block = 200; Cb=160 Cr=100 */
    int yv[4] = { 200, 200, 200, 200 };
    u32 n = build_jpeg(jpg, sizeof(jpg), 1, 8, 8, yv, 160, 100, 0);
    if (check("444 jpeg built", n > 100) != 0) return 1;
    { FILE *f = fopen("/tmp/444.jpg", "wb"); fwrite(jpg, 1, n, f); fclose(f); }
    u32 w = 0, h = 0;
    int rc = lib_mjpeg_decode(jpg, n, rgb, sizeof(rgb), &w, &h, 0);
    if (check("444 decode rc=0", rc == 0) != 0) {
        printf("  err: %s\n", lib_mjpeg_last_error());
        return 1;
    }
    check("444 size 8x8", w == 8 && h == 8);
    /* expected RGB for (Y=200, Cb=160, Cr=100) via BT.601 integer */
    int eR = 200 + ((91881 * (100 - 128)) >> 16);
    int eG = 200 - ((22554 * (160 - 128) + 46802 * (100 - 128)) >> 16);
    int eB = 200 + ((116130 * (160 - 128)) >> 16);
    if (eR < 0) eR = 0;
    if (eR > 255) eR = 255;
    if (eG < 0) eG = 0;
    if (eG > 255) eG = 255;
    if (eB < 0) eB = 0;
    if (eB > 255) eB = 255;
    printf("  expected RGB = (%d,%d,%d)\n", eR, eG, eB);
    int bad = 0;
    for (u32 i = 0; i < w * h; i++)
        if (rgb[i*3] != eR || rgb[i*3+1] != eG || rgb[i*3+2] != eB) bad++;
    return check("444 all pixels expected RGB", bad == 0);
}

static int test_jpeg_420(void)
{
    u8 jpg[8192], rgb[32 * 32 * 3];
    int yv[4] = { 200, 150, 100, 50 };
    u32 n = build_jpeg(jpg, sizeof(jpg), 2, 16, 16, yv, 160, 100, 0);
    if (check("420 jpeg built", n > 100) != 0) return 1;
    u32 w = 0, h = 0;
    int rc = lib_mjpeg_decode(jpg, n, rgb, sizeof(rgb), &w, &h, 0);
    if (check("420 decode rc=0", rc == 0) != 0) {
        printf("  err: %s\n", lib_mjpeg_last_error());
        return 1;
    }
    check("420 size 16x16", w == 16 && h == 16);
    /* quadrant check: luma varies per quadrant, chroma is uniform
     * (Cb=160, Cr=100) -> RGB = BT.601(Y, 160, 100) */
    int dCb = 160 - 128, dCr = 100 - 128;
    int oR = (91881 * dCr) >> 16;
    int oG = -((22554 * dCb + 46802 * dCr) >> 16);
    int oB = (116130 * dCb) >> 16;
    int quad[4] = { 200, 150, 100, 50 };
    int ok = 1;
    for (int q = 0; q < 4; q++) {
        int eR = quad[q] + oR; if (eR < 0) eR = 0; if (eR > 255) eR = 255;
        int eG = quad[q] + oG; if (eG < 0) eG = 0; if (eG > 255) eG = 255;
        int eB = quad[q] + oB; if (eB < 0) eB = 0; if (eB > 255) eB = 255;
        u32 px = ((q & 1) ? 15 : 0) + ((q & 2) ? 15 : 0) * 16;
        if (rgb[px*3] != eR || rgb[px*3+1] != eG || rgb[px*3+2] != eB)
            { ok = 0; printf("  quad%d expected (%d,%d,%d) got (%d,%d,%d)\n",
                             q, eR, eG, eB, rgb[px*3], rgb[px*3+1],
                             rgb[px*3+2]); }
    }
    return check("420 quadrant values correct", ok);
}

static int test_jpeg_dri(void)
{
    u8 jpg[8192], rgb[64 * 64 * 3];
    int yv[1] = { 180 };
    u32 n = build_jpeg(jpg, sizeof(jpg), 0, 32, 32, yv, 0, 0, 1);
    if (check("dri jpeg built", n > 100) != 0) return 1;
    { FILE *f = fopen("/tmp/dri.jpg", "wb"); fwrite(jpg, 1, n, f); fclose(f); }
    u32 w = 0, h = 0;
    int rc = lib_mjpeg_decode(jpg, n, rgb, sizeof(rgb), &w, &h, 0);
    if (check("dri(1) decode rc=0", rc == 0) != 0) {
        printf("  err: %s\n", lib_mjpeg_last_error());
        return 1;
    }
    int bad = 0;
    for (u32 i = 0; i < w * h; i++)
        if (rgb[i*3] != 180) bad++;
    return check("dri 32x32 all pixels 180", bad == 0);
}

static int test_jpeg_rejects(void)
{
    u8 jpg[8192], rgb[1024];
    int yv[1] = { 200 };
    int fails = 0;
    /* truncated stream */
    u32 n = build_jpeg(jpg, sizeof(jpg), 0, 8, 8, yv, 0, 0, 0);
    fails += check("truncated jpeg rejected",
                   lib_mjpeg_decode(jpg, n / 2, rgb, sizeof(rgb), 0, 0, 0) == -1);
    /* progressive header (SOF2) hand-built */
    u8 prog[32] = {
        0xFF, 0xD8, 0xFF, 0xC2, 0x00, 0x0E, 0x08, 0x00,
        0x08, 0x00, 0x08, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xD9
    };
    fails += check("progressive jpeg rejected",
                   lib_mjpeg_decode(prog, 17, rgb, sizeof(rgb),
                                    0, 0, 0) == -1);
    /* no SOI */
    u8 nosoi[8] = { 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0 };
    fails += check("missing SOI rejected",
                   lib_mjpeg_decode(nosoi, 8, rgb, sizeof(rgb), 0, 0, 0) == -1);
    /* buffer too small -> -2 */
    n = build_jpeg(jpg, sizeof(jpg), 0, 16, 16, yv, 0, 0, 0);
    fails += check("small output buffer -> -2",
                   lib_mjpeg_decode(jpg, n, rgb, 100, 0, 0, 0) == -2);
    return fails;
}

/* ================================================================== */
/* 4. YUY2 converter                                                   */
/* ================================================================== */

static int test_yuy2(void)
{
    /* 4x2 image: Y=200, Cb=160, Cr=100 for both pixel pairs */
    u8 yuy2[4 * 2 * 4];
    for (int i = 0; i < 8; i++) {
        yuy2[i*4+0] = 200; yuy2[i*4+1] = 160;
        yuy2[i*4+2] = 200; yuy2[i*4+3] = 100;
    }
    u8 rgb[4 * 2 * 3];
    camera_yuy2_to_rgb(yuy2, 4, 2, rgb, sizeof(rgb));
    int eR = 200 + ((91881 * (100 - 128)) >> 16);
    int eG = 200 - ((22554 * (160 - 128) + 46802 * (100 - 128)) >> 16);
    int eB = 200 + ((116130 * (160 - 128)) >> 16);
    eR = eR < 0 ? 0 : (eR > 255 ? 255 : eR);
    eG = eG < 0 ? 0 : (eG > 255 ? 255 : eG);
    eB = eB < 0 ? 0 : (eB > 255 ? 255 : eB);
    int ok = 1;
    for (u32 i = 0; i < 8; i++)
        if (rgb[i*3] != eR || rgb[i*3+1] != eG || rgb[i*3+2] != eB) ok = 0;
    return check("yuy2->rgb BT.601 values", ok);
}

/* ================================================================== */
/* 5. USB fake (scripted control + bulk)                               */
/* ================================================================== */

typedef struct {
    u8  rt, req;
    u16 wv, wi;
    u8  data[64];
    u16 len;
    int rc;
    int hits;
} ctrl_script_t;

static ctrl_script_t g_ctrl[24];
static int g_ctrl_n;

static void ctrl_reset(void) { g_ctrl_n = 0; }
static void ctrl_add(u8 rt, u8 req, u16 wv, u16 wi,
                     const u8 *data, u16 len, int rc)
{
    g_ctrl[g_ctrl_n].rt = rt;
    g_ctrl[g_ctrl_n].req = req;
    g_ctrl[g_ctrl_n].wv = wv;
    g_ctrl[g_ctrl_n].wi = wi;
    if (data && len) memcpy(g_ctrl[g_ctrl_n].data, data, len);
    g_ctrl[g_ctrl_n].len = len;
    g_ctrl[g_ctrl_n].rc = rc;
    g_ctrl[g_ctrl_n].hits = 0;
    g_ctrl_n++;
}

static u8   g_bulk[64][2048];
static u32  g_bulk_len[64];
static int  g_bulk_n, g_bulk_i;
static int  g_setiface_calls, g_setiface_if, g_setiface_alt;

static void bulk_reset(void)
{
    g_bulk_n = 0; g_bulk_i = 0;
    g_setiface_calls = 0;
}

static int fake_control(driver_usb_dev_t *d, const driver_usb_setup_t *s,
                        void *buf, u16 len)
{
    (void)d;
    for (int i = 0; i < g_ctrl_n; i++) {
        ctrl_script_t *c = &g_ctrl[i];
        if (c->rt == s->bmRequestType && c->req == s->bRequest &&
            c->wv == s->wValue && c->wi == s->wIndex) {
            c->hits++;
            u16 n = c->len < len ? c->len : len;
            if (n) memcpy(buf, c->data, n);
            return c->rc;
        }
    }
    return -1;
}

static int g_bulk_trace;
static int fake_bulk(driver_usb_dev_t *d, u8 ep, void *buf, u16 len)
{
    (void)d; (void)ep;
    if (g_bulk_i >= g_bulk_n) return OC_USB_ETIMEDOUT;
    u32 n = g_bulk_len[g_bulk_i];
    if (n > len) n = len;
    memcpy(buf, g_bulk[g_bulk_i], n);
    if (g_bulk_trace)
        printf("  bulk[%d] -> %u bytes hdr=%02x %02x\n", g_bulk_i, n,
               g_bulk[g_bulk_i][0], g_bulk[g_bulk_i][1]);
    g_bulk_i++;
    return (int)n;
}

static int fake_iso(driver_usb_dev_t *d, u8 ep, void *buf, u16 len)
{
    return fake_bulk(d, ep, buf, len);
}

static int fake_set_iface(driver_usb_dev_t *d, u16 iface, u16 alt)
{
    (void)d;
    g_setiface_calls++;
    g_setiface_if = iface;
    g_setiface_alt = alt;
    return 0;
}

static int fake_register_driver(const char *name, u8 class_code,
                                driver_usb_probe_fn probe,
                                driver_usb_disconnect_fn disc)
{
    (void)name; (void)class_code; (void)probe; (void)disc;
    return 0;
}

/* replacements for the USB core entry points the UVC driver calls */
int driver_usb_control_transfer(driver_usb_dev_t *d,
                                const driver_usb_setup_t *s,
                                void *buf, u16 len)
{ return fake_control(d, s, buf, len); }
int driver_usb_bulk_transfer(driver_usb_dev_t *d, u8 ep, void *buf, u16 len)
{ return fake_bulk(d, ep, buf, len); }
int driver_usb_isochronous_transfer(driver_usb_dev_t *d, u8 ep, void *buf,
                                    u16 len)
{ return fake_iso(d, ep, buf, len); }
int driver_usb_set_interface(driver_usb_dev_t *d, u16 i, u16 a)
{ return fake_set_iface(d, i, a); }
int driver_usb_register_driver(const char *name, u8 cc,
                               driver_usb_probe_fn p,
                               driver_usb_disconnect_fn dc)
{ return fake_register_driver(name, cc, p, dc); }

/* ================================================================== */
/* 6. UVC descriptor fixture (UVC 1.1, MJPEG + YUY2)                   */
/* ================================================================== */

static u8  g_desc[512];
static u32 g_desc_len;

static void desc_put16(u32 off, u16 v)
{ g_desc[off] = (u8)v; g_desc[off + 1] = (u8)(v >> 8); }

static u32 build_desc(void)
{
    u8 *p = g_desc;
    u32 cfg_len_off, vc_hdr_len_off, vs_hdr_len_off;

    /* config descriptor */
    p[0] = 9; p[1] = 0x02;
    cfg_len_off = 2;
    p[4] = 1; p[5] = 1; p[6] = 3; p[7] = 0x80; p[8] = 250;
    p += 9;

    /* IAD */
    p[0] = 8; p[1] = 0x0B; p[2] = 0; p[3] = 2;
    p[4] = 0x0E; p[5] = 0x03; p[6] = 0; p[7] = 0;
    p += 8;

    /* VC interface alt0 */
    p[0] = 9; p[1] = 0x04; p[2] = 0; p[3] = 0;
    p[4] = 1; p[5] = 0x0E; p[6] = 0x01; p[7] = 0; p[8] = 0;
    p += 9;

    vc_hdr_len_off = (u32)(p - g_desc) + 5;
    /* VC CS header (UVC 1.1, 13 bytes): bcdUVC(3-4) wTotalLength(5-6)
     * dwClockFrequency(7-10) bInCollection(11) baInterfaceNr[](12) */
    p[0] = 13; p[1] = 0x24; p[2] = 0x01;
    p[3] = 0x10; p[4] = 0x01;                   /* bcdUVC = 0x0110 */
    /* wTotalLength patched below */
    p[7] = 0x00; p[8] = 0x00; p[9] = 0x00; p[10] = 0x00;  /* clock */
    p[11] = 0x01;                               /* bInCollection   */
    p[12] = 0x01;                               /* baInterfaceNr[0] */
    p += 13;

    /* VC input terminal (camera): id 1, type 0x0201 */
    p[0] = 18; p[1] = 0x24; p[2] = 0x02;
    p[3] = 1;
    p[4] = 0x01; p[5] = 0x02;                   /* wTerminalType 0x0201 */
    p[6] = 0; p[7] = 0;
    p[8] = 3;
    p[9] = 0x00; p[10] = 0x00; p[11] = 0x0A;
    p[12] = 0; p[13] = 0; p[14] = 0;
    p[15] = 0; p[16] = 0; p[17] = 0;
    p += 18;

    /* VC processing unit: id 2, source 1 */
    p[0] = 13; p[1] = 0x24; p[2] = 0x05;
    p[3] = 2;
    p[4] = 1;
    p[5] = 5; p[6] = 0; p[7] = 0; p[8] = 0;
    p[9] = 3;
    p[10] = 0x00; p[11] = 0x00; p[12] = 0x7F;
    p += 13;

    /* VC output terminal: id 3, type 0x0101 */
    p[0] = 9; p[1] = 0x24; p[2] = 0x03;
    p[3] = 3;
    p[4] = 0x01; p[5] = 0x01;
    p[6] = 0; p[7] = 0;
    p[8] = 0;
    p += 9;

    /* VC status interrupt EP */
    p[0] = 7; p[1] = 0x05; p[2] = 0x81; p[3] = 0x03;
    p[4] = 0x10; p[5] = 0; p[6] = 8;
    p += 7;

    /* patch VC wTotalLength (from VC CS header through EP) */
    desc_put16(vc_hdr_len_off, (u16)((p - g_desc) -
               (vc_hdr_len_off - 5)));

    /* VS interface alt0 (zero bandwidth) */
    p[0] = 9; p[1] = 0x04; p[2] = 1; p[3] = 0;
    p[4] = 0; p[5] = 0x0E; p[6] = 0x02; p[7] = 0; p[8] = 0;
    p += 9;

    vs_hdr_len_off = (u32)(p - g_desc) + 4;
    /* VS CS input header: 2 formats */
    p[0] = 13; p[1] = 0x24; p[2] = 0x01;
    p[3] = 2;
    /* wTotalLength patched below */
    p[6] = 0x82;                                /* ep addr */
    p[7] = 0; p[8] = 0; p[9] = 0; p[10] = 0; p[11] = 0; p[12] = 0;
    p += 13;
    u32 vs_start = vs_hdr_len_off - 4 + 13;     /* first format byte */

    /* VS CS format MJPEG (idx 1) */
    p[0] = 11; p[1] = 0x24; p[2] = 0x07;
    p[3] = 1;
    p[4] = 1;
    p[5] = 0x01;
    p[6] = 1; p[7] = 0; p[8] = 0;
    p[9] = 0; p[10] = 0;
    p += 11;

    /* VS CS frame MJPEG (idx 1): 640x480, 2 discrete intervals.
     * Exact UVC layout: bFrameIndex(3) bmCaps(4) wWidth(5-6)
     * wHeight(7-8) dwMinBitRate(9-12) dwMaxBitRate(13-16)
     * dwMaxVideoFrameBufferSize(17-20) dwDefaultFrameInterval(21-24)
     * bFrameIntervalType(25) dwFrameInterval[0](26-29) [1](30-33)
     * -> bLength 34 */
    {
        u8 *f = p;
        f[0] = 34; f[1] = 0x24; f[2] = 0x08;
        f[3] = 1;                                /* bFrameIndex */
        f[4] = 0x01;                             /* bmCaps      */
        f[5] = 0x80; f[6] = 0x02;                /* w = 640     */
        f[7] = 0xE0; f[8] = 0x01;                /* h = 480     */
        f[9]  = 0x00; f[10] = 0x4C; f[11] = 0x04; f[12] = 0x00;
        f[13] = 0x00; f[14] = 0x4C; f[15] = 0x04; f[16] = 0x00;
        f[17] = 0x00; f[18] = 0x30; f[19] = 0x07; f[20] = 0x00;
        f[21] = 0x2A; f[22] = 0x2C; f[23] = 0x0A; f[24] = 0x00;
        f[25] = 2;                               /* discrete x2 */
        f[26] = 0x2A; f[27] = 0x2C; f[28] = 0x0A; f[29] = 0x00; /* 666666 */
        f[30] = 0x55; f[31] = 0x58; f[32] = 0x14; f[33] = 0x00; /* 1333333 */
        p += 34;
    }

    /* VS CS format uncompressed (idx 2): GUID YUY2 */
    {
        u8 *f = p;
        f[0] = 27; f[1] = 0x24; f[2] = 0x04;
        f[3] = 2;                                /* bFormatIndex */
        f[4] = 1;                                /* bNumFrameDescriptors */
        memcpy(f + 5, "YUY2", 4);
        f[9]  = 0x00; f[10] = 0x00; f[11] = 0x10; f[12] = 0x00;
        f[13] = 0x80; f[14] = 0x00; f[15] = 0x00; f[16] = 0xAA;
        f[17] = 0x00; f[18] = 0x38; f[19] = 0x9B; f[20] = 0x71;
        f[21] = 16;                              /* bBitsPerPixel */
        f[22] = 1;                               /* bDefaultFrameIndex */
        f[23] = 0x01;                            /* bAspectRatioX */
        f[24] = 0x01;                            /* bAspectRatioY */
        f[25] = 0;                               /* bmInterlaceFlags */
        f[26] = 0;                               /* bCopyProtect */
        p += 27;
    }

    /* VS CS frame uncompressed (idx 1): 320x240, 1 discrete interval
     * (same offsets as the MJPEG frame; bLength = 30 for 1 interval) */
    {
        u8 *f = p;
        f[0] = 30; f[1] = 0x24; f[2] = 0x05;
        f[3] = 1;
        f[4] = 0x01;
        f[5] = 0x40; f[6] = 0x01;                /* 320 */
        f[7] = 0xF0; f[8] = 0x00;                /* 240 */
        f[9]  = 0x00; f[10] = 0x96; f[11] = 0x02; f[12] = 0x00;
        f[13] = 0x00; f[14] = 0x96; f[15] = 0x02; f[16] = 0x00;
        f[17] = 0x00; f[18] = 0x4B; f[19] = 0x03; f[20] = 0x00;
        f[21] = 0x2A; f[22] = 0x2C; f[23] = 0x0A; f[24] = 0x00;
        f[25] = 1;
        f[26] = 0x2A; f[27] = 0x2C; f[28] = 0x0A; f[29] = 0x00;
        p += 30;
    }

    /* VS isochronous EP (alt0 side: still listed before alt1 iface) */
    p[0] = 7; p[1] = 0x05; p[2] = 0x82; p[3] = 0x01;
    p[4] = 0x00; p[5] = 0x04; p[6] = 1;          /* 1024 bytes, 1 mf */
    p += 7;

    /* VS interface alt1 (streaming) */
    p[0] = 9; p[1] = 0x04; p[2] = 1; p[3] = 1;
    p[4] = 1; p[5] = 0x0E; p[6] = 0x02; p[7] = 0; p[8] = 0;
    p += 9;

    /* VS isochronous EP (streaming bandwidth) */
    p[0] = 7; p[1] = 0x05; p[2] = 0x82; p[3] = 0x01;
    p[4] = 0x00; p[5] = 0x04; p[6] = 1;
    p += 7;

    u32 total = (u32)(p - g_desc);
    desc_put16(cfg_len_off, (u16)total);
    desc_put16(vs_hdr_len_off, (u16)(total - vs_start));
    g_desc_len = total;
    return total;
}

/* ================================================================== */
/* 7. camera core tests                                                */
/* ================================================================== */

static int core_probe_ok, core_commit_ok, core_start_ok, core_stop_ok;
static int core_cap_calls, core_ctrl_set_calls, core_ctrl_last;

static int core_probe(camera_dev_t *c, u8 f, u8 fr, u32 iv)
{ (void)c; core_probe_ok = (f == 1 && fr == 1 && iv == 666666); return 0; }
static int core_commit(camera_dev_t *c) { (void)c; core_commit_ok = 1; return 0; }
static int core_start(camera_dev_t *c)  { (void)c; core_start_ok = 1; return 0; }
static int core_stop(camera_dev_t *c)   { (void)c; core_stop_ok = 1; return 0; }
static int core_capture(camera_dev_t *c, u8 *o, u32 m, cam_frame_meta_t *mt)
{ (void)o; (void)m; core_cap_calls++;
  if (mt) { mt->w = 640; mt->h = 480; mt->seq = core_cap_calls;
            mt->ts_ms = 99; mt->dropped = 0; }
  return c ? 0 : -1; }
static int core_ctrl_set(camera_dev_t *c, u8 unit, u8 cs, i32 v)
{ (void)c; (void)unit; (void)cs; core_ctrl_set_calls++; core_ctrl_last = v; return 0; }
static int core_ctrl_get(camera_dev_t *c, u8 unit, u8 cs, i32 *v)
{ (void)c; (void)unit; (void)cs; *v = 111; return 0; }

static const camera_ops_t core_ops = {
    .probe = core_probe, .commit = core_commit,
    .stream_start = core_start, .stream_stop = core_stop,
    .capture = core_capture, .ctrl_set = core_ctrl_set,
    .ctrl_get = core_ctrl_get,
};

static int test_camera_core(void)
{
    int fails = 0;
    camera_dev_t cam;
    memset(&cam, 0, sizeof(cam));
    cam.ops = &core_ops;

    fails += check("camera_count starts 0", camera_count() == 0);
    fails += check("register returns 0", camera_register(&cam) == 0);
    fails += check("double register rejected", camera_register(&cam) == -1);
    fails += check("camera_count 1", camera_count() == 1);
    fails += check("camera_get(0) named", camera_get(0) == &cam &&
                   cam.name[0] == 'c');
    fails += check("camera_get(5) NULL", camera_get(5) == 0);

    /* format table: MJPEG 640x480@15/7.5, YUY2 320x240@15 */
    cam.n_formats = 2;
    cam.formats[0].fmt = CAM_FMT_MJPEG;
    cam.formats[0].n_frames = 1;
    cam.formats[0].frames[0].w = 640;
    cam.formats[0].frames[0].h = 480;
    cam.formats[0].frames[0].n_intervals = 2;
    cam.formats[0].frames[0].intervals[0] = 666666;
    cam.formats[0].frames[0].intervals[1] = 1333333;
    cam.formats[1].fmt = CAM_FMT_YUY2;
    cam.formats[1].n_frames = 1;
    cam.formats[1].frames[0].w = 320;
    cam.formats[1].frames[0].h = 240;
    cam.formats[1].frames[0].n_intervals = 1;
    cam.formats[1].frames[0].intervals[0] = 666666;

    u8 fmts[4];
    fails += check("get_formats count 2",
                   camera_get_formats(&cam, fmts, 4) == 2 &&
                   fmts[0] == CAM_FMT_MJPEG && fmts[1] == CAM_FMT_YUY2);
    u32 ws[2], hs[2];
    fails += check("get_frames MJPEG 640x480",
                   camera_get_frames(&cam, CAM_FMT_MJPEG, ws, hs, 2) == 1 &&
                   ws[0] == 640 && hs[0] == 480);
    fails += check("set_format MJPEG", camera_stream_set_format(&cam,
                   CAM_FMT_MJPEG) == 0);
    fails += check("set_format unknown rejected",
                   camera_stream_set_format(&cam, CAM_FMT_NV12) == -1);
    fails += check("set_frame 640x480", camera_stream_set_frame(&cam, 640,
                   480) == 0);
    fails += check("set_frame unknown rejected", camera_stream_set_frame(&cam,
                   320, 240) == -1);
    fails += check("set_interval 666666", camera_stream_set_interval(&cam,
                   666666) == 0);
    fails += check("set_interval unknown rejected",
                   camera_stream_set_interval(&cam, 12345) == -1);

    fails += check("capture refused before start",
                   camera_capture_frame(&cam, 0, 0, 0) == -1);
    fails += check("stream_start", camera_stream_start(&cam) == 0 &&
                   core_start_ok && cam.streaming == 1);
    cam_frame_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    fails += check("capture works while streaming",
                   camera_capture_frame(&cam, 0, 0, &meta) == 0 &&
                   core_cap_calls == 1 && meta.w == 640 && meta.h == 480 &&
                   meta.ts_ms == 99);
    fails += check("release_frame", camera_release_frame(&cam, &meta) == 0);
    fails += check("stream_stop", camera_stream_stop(&cam) == 0 &&
                   core_stop_ok && cam.streaming == 0);

    /* controls: no range -> passthrough */
    fails += check("set_brightness passthrough",
                   camera_set_brightness(&cam, 42) == 0 &&
                   core_ctrl_last == 42);
    /* with range: clamp + step */
    cam.rng[0][0] = 0; cam.rng[0][1] = 100; cam.rng[0][2] = 10;
    fails += check("brightness clamped to range",
                   camera_set_brightness(&cam, 999) == 0 &&
                   core_ctrl_last == 100);
    fails += check("brightness step-snapped",
                   camera_set_brightness(&cam, 47) == 0 &&
                   core_ctrl_last == 40);
    fails += check("caps passthrough", camera_get_caps(&cam) == 0);
    cam.caps = CAM_CAP_MJPEG | CAM_CAP_STREAMING;
    fails += check("caps reported", camera_get_caps(&cam) ==
                   (CAM_CAP_MJPEG | CAM_CAP_STREAMING));

    fails += check("unregister", camera_unregister(&cam) == 0 &&
                   camera_count() == 0);
    return fails;
}

/* ================================================================== */
/* 8. UVC driver tests (descriptor parse + probe/commit + capture)     */
/* ================================================================== */

static driver_usb_dev_t make_usb_dev(void)
{
    driver_usb_dev_t d;
    memset(&d, 0, sizeof(d));
    d.present = 1;
    d.slot = 1;
    d.addr = 1;
    d.speed = USB_SPEED_HS;
    d.vid = 0x046D; d.pid = 0x0825;      /* real UVC cam IDs */
    d.cfg_len = (u16)g_desc_len;
    memcpy(d.cfg_raw, g_desc, g_desc_len);
    return d;
}

/* probe/commit control script for the fake USB:
 * GET_LEN(probe) = 26 (UVC 1.0/1.1 core), GET_CUR(probe) echoes the
 * negotiated values, ranges for the six controls. */
static void setup_ctrl_script(void)
{
    ctrl_reset();
    u8 len2[2] = { 26, 0 };
    /* All wIndex values follow the UVC class-request rule:
     * VS controls: (vs_if << 8) | 0  = 0x0100 (vs_if = 1)
     * unit controls: (unit << 8) | vc_if  (vc_if = 0)             */
    /* probe control (VS, cs=0x01): length + echo */
    ctrl_add(0xA1, VC_REQ_GET_LEN, 0x0100, 0x0100, len2, 2, 0);
    u8 cur[26];
    memset(cur, 0, sizeof(cur));
    cur[2] = 1; cur[3] = 1;
    cur[4] = 0x2A; cur[5] = 0x2C; cur[6] = 0x0A; cur[7] = 0x00;
    cur[18] = 0x00; cur[19] = 0x08; cur[20] = 0x07; cur[21] = 0x00; /* 460800 */
    cur[22] = 0x00; cur[23] = 0x01; cur[24] = 0x00; cur[25] = 0x00; /* 256    */
    ctrl_add(0xA1, VC_REQ_GET_CUR, 0x0100, 0x0100, cur, 26, 0);
    /* probe SET_CUR (accepted) */
    ctrl_add(0x21, VC_REQ_SET_CUR, 0x0100, 0x0100, 0, 0, 0);
    /* commit SET_CUR (cs=0x02) */
    ctrl_add(0x21, VC_REQ_SET_CUR, 0x0200, 0x0100, 0, 0, 0);

    /* control ranges: PU unit 2 (0x0200) / CT unit 1 (0x0100) */
    u8 mn[2] = { 0x00, 0x00 }, mx[2] = { 0x64, 0x00 }, rs[2] = { 0x0A, 0x00 };
    /* brightness cs=0x02 */
    ctrl_add(0xA1, VC_REQ_GET_MIN, 0x0200, 0x0200, mn, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_MAX, 0x0200, 0x0200, mx, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_RES, 0x0200, 0x0200, rs, 2, 0);
    /* contrast cs=0x03 */
    ctrl_add(0xA1, VC_REQ_GET_MIN, 0x0300, 0x0200, mn, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_MAX, 0x0300, 0x0200, mx, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_RES, 0x0300, 0x0200, rs, 2, 0);
    /* saturation cs=0x07 */
    ctrl_add(0xA1, VC_REQ_GET_MIN, 0x0700, 0x0200, mn, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_MAX, 0x0700, 0x0200, mx, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_RES, 0x0700, 0x0200, rs, 2, 0);
    /* exposure cs=0x03 (CT unit 1) */
    ctrl_add(0xA1, VC_REQ_GET_MIN, 0x0300, 0x0100, mn, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_MAX, 0x0300, 0x0100, mx, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_RES, 0x0300, 0x0100, rs, 2, 0);
    /* focus cs=0x05 (CT) */
    ctrl_add(0xA1, VC_REQ_GET_MIN, 0x0500, 0x0100, mn, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_MAX, 0x0500, 0x0100, mx, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_RES, 0x0500, 0x0100, rs, 2, 0);
    /* zoom cs=0x0b (CT) */
    ctrl_add(0xA1, VC_REQ_GET_MIN, 0x0B00, 0x0100, mn, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_MAX, 0x0B00, 0x0100, mx, 2, 0);
    ctrl_add(0xA1, VC_REQ_GET_RES, 0x0B00, 0x0100, rs, 2, 0);
}

/* build two complete frames of UVC bulk payloads around one JPEG:
 * frame = [hdr FID=0][half] + [hdr FID=0 EOF=1][rest], then a second
 * frame with FID=1.  Queue: f1a f1b f2a f2b. */
static void queue_bulk_frames(const u8 *jpg, u32 jn)
{
    bulk_reset();
    u32 half = jn / 2;
    u8 *pk = (u8 *)g_bulk[0];

    /* payload header bmHeaderInfo bits (UVC Table 2-5):
     * bit0 = FID (frame toggle), bit1 = EOF (end of frame) */
    /* frame 1, packet a: FID=0 */
    pk[0] = 12; pk[1] = 0x00;
    memcpy(pk + 12, jpg, half);
    g_bulk_len[g_bulk_n++] = 12 + half;
    /* frame 1, packet b: FID=0 | EOF */
    pk = g_bulk[g_bulk_n];
    pk[0] = 12; pk[1] = 0x02;
    memcpy(pk + 12, jpg + half, jn - half);
    g_bulk_len[g_bulk_n++] = 12 + (jn - half);

    /* frame 2, packet a: FID=1 */
    pk = g_bulk[g_bulk_n];
    pk[0] = 12; pk[1] = 0x01;
    memcpy(pk + 12, jpg, half);
    g_bulk_len[g_bulk_n++] = 12 + half;
    /* frame 2, packet b: FID=1 | EOF */
    pk = g_bulk[g_bulk_n];
    pk[0] = 12; pk[1] = 0x03;
    memcpy(pk + 12, jpg + half, jn - half);
    g_bulk_len[g_bulk_n++] = 12 + (jn - half);
}

static int test_uvc_parse_and_stream(void)
{
    int fails = 0;
    camera_init();

    build_desc();
    driver_usb_dev_t dev = make_usb_dev();

    setup_ctrl_script();

    fails += check("uvc bind via probe", uvc_usb_probe(&dev) == 0);
    fails += check("uvc count 1", g_uvc_n == 1 &&
                   driver_camera_uvc_count() == 1);
    camera_dev_t *cam = camera_get(0);
    if (!cam) { check("camera registered", 0); return fails + 1; }
    fails += check("camera named camera0", cam->name[0] == 'c');
    fails += check("uvc version 1.1 (bcd 0x0110)",
                   cam->uvc_version_major == 1 &&
                   cam->uvc_version_minor == 0x10);
    fails += check("two formats parsed", cam->n_formats == 2);
    fails += check("format 1 MJPEG",
                   cam->formats[0].fmt == CAM_FMT_MJPEG);
    fails += check("MJPEG frame 640x480",
                   cam->formats[0].n_frames == 1 &&
                   cam->formats[0].frames[0].w == 640 &&
                   cam->formats[0].frames[0].h == 480);
    fails += check("MJPEG intervals 666666/1333333",
                   cam->formats[0].frames[0].n_intervals == 2 &&
                   cam->formats[0].frames[0].intervals[0] == 666666 &&
                   cam->formats[0].frames[0].intervals[1] == 1333333);
    fails += check("format 2 YUY2",
                   cam->formats[1].fmt == CAM_FMT_YUY2 &&
                   cam->formats[1].frames[0].w == 320 &&
                   cam->formats[1].frames[0].h == 240);
    printf("  desc[20..70]:");
    for (int q = 20; q < 70; q++) printf(" %02x", g_desc[q]);
    printf("\n");
    printf("  ct=%u pu=%u fmts=%u iv0=%u iv1=%u rng=%d/%d/%d\n",
           cam->ct_unit, cam->pu_unit, cam->n_formats,
           cam->formats[0].frames[0].intervals[0],
           cam->formats[0].frames[0].intervals[1],
           cam->rng[0][0], cam->rng[0][1], cam->rng[0][2]);
    fails += check("terminals found (CT=1 PU=2)",
                   cam->ct_unit == 1 && cam->pu_unit == 2);
    fails += check("caps: streaming+mjpeg+yuy2+iso+CT+PU",
                   (cam->caps & CAM_CAP_STREAMING) &&
                   (cam->caps & CAM_CAP_MJPEG) &&
                   (cam->caps & CAM_CAP_YUY2) &&
                   (cam->caps & CAM_CAP_ISO) &&
                   (cam->caps & CAM_CAP_TERMINAL_CT) &&
                   (cam->caps & CAM_CAP_TERMINAL_PU));
    fails += check("brightness range from GET_MIN/MAX/RES",
                   cam->rng[0][0] == 0 && cam->rng[0][1] == 100 &&
                   cam->rng[0][2] == 10);

    /* descriptor dump */
    u8 dump[512];
    int dn = driver_camera_uvc_descriptors(cam, dump, sizeof(dump));
    fails += check("descriptor dump", dn > 0 && memcmp(dump, g_desc,
                   (u32)dn) == 0);

    /* streaming: select MJPEG 640x480 @15fps, probe+commit+start */
    fails += check("set format MJPEG",
                   camera_stream_set_format(cam, CAM_FMT_MJPEG) == 0);
    fails += check("set frame", camera_stream_set_frame(cam, 640, 480) == 0);
    fails += check("set interval", camera_stream_set_interval(cam,
                   666666) == 0);
    fails += check("probe", cam->ops->probe(cam, cam->cur_fmt + 1,
                   cam->cur_frame + 1, 666666) == 0);
    fails += check("probe echoes frame size", 
                   ((uvc_cam_t *)cam->priv)->probe.dwMaxVideoFrameSize
                   == 460800);
    fails += check("commit", cam->ops->commit(cam) == 0);
    fails += check("start switches alt setting",
                   cam->ops->stream_start(cam) == 0 &&
                   g_setiface_calls == 1 && g_setiface_if == 1 &&
                   g_setiface_alt == 1);

    /* capture: queue 2 real frames + timeout; frame 1 must decode */
    u8 jpg[8192];
    int yv[1] = { 170 };
    u32 jn = build_jpeg(jpg, sizeof(jpg), 0, 32, 32, yv, 0, 0, 0);
    queue_bulk_frames(jpg, jn);
    g_bulk_trace = 1;
    u8 rgb[64 * 64 * 3];
    cam_frame_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    int rc = cam->ops->capture(cam, rgb, sizeof(rgb), &meta);
    fails += check("capture frame 1 decodes", rc == 0 &&
                   meta.w == 32 && meta.h == 32 && meta.seq == 1);
    if (rc == 0) {
        int bad = 0;
        for (u32 i = 0; i < 32u * 32u; i++)
            if (rgb[i*3] != 170) bad++;
        fails += check("capture pixels correct", bad == 0);
    }
    /* second frame (FID toggle) */
    memset(&meta, 0, sizeof(meta));
    rc = cam->ops->capture(cam, rgb, sizeof(rgb), &meta);
    printf("  frame2 rc=%d seq=%u dropped=%u err=%s\n", rc, meta.seq,
           meta.dropped, lib_mjpeg_last_error());
    { uvc_cam_t *uc2 = (uvc_cam_t *)cam->priv;
      printf("  ring[0..15]:"); for (int q = 0; q < 16; q++)
          printf(" %02x", uc2->ring[0][q]);
      printf("\n  jpg [0..15]:"); for (int q = 0; q < 16; q++)
          printf(" %02x", jpg[q]);
      u32 half = jn / 2;
      printf("\n  ring[half..+7]:"); for (int q = 0; q < 8; q++)
          printf(" %02x", uc2->ring[0][half + q]);
      printf("\n  jpg [half..+7]:"); for (int q = 0; q < 8; q++)
          printf(" %02x", jpg[half + q]);
      printf("\n  ring[half-8..half):"); for (int q = 8; q > 0; q--)
          printf(" %02x", uc2->ring[0][half - q]);
      printf("\n  jpg [half-8..half):"); for (int q = 8; q > 0; q--)
          printf(" %02x", jpg[half - q]);
      printf("\n"); }
    fails += check("capture frame 2 decodes", rc == 0 && meta.seq == 2);
    /* drained: timeout reported honestly */
    fails += check("no frame -> honest -1", cam->ops->capture(cam, rgb,
                   sizeof(rgb), &meta) == -1);

    /* terminal controls through the fake USB: brightness SET_CUR
     * (PU unit 2 -> wIndex 0x0200, cs 0x02 -> wValue 0x0200) */
    ctrl_add(0x21, VC_REQ_SET_CUR, 0x0200, 0x0200, 0, 0, 0);
    fails += check("set_brightness via USB", camera_set_brightness(cam,
                   33) == 0);
    /* exposure SET_CUR (CT unit 1 -> wIndex 0x0100, cs 0x03) */
    ctrl_add(0x21, VC_REQ_SET_CUR, 0x0300, 0x0100, 0, 0, 0);
    fails += check("set_exposure via USB", camera_set_exposure(cam,
                   100) == 0);

    fails += check("stop returns alt0", cam->ops->stream_stop(cam) == 0 &&
                   g_setiface_if == 1 && g_setiface_alt == 0);

    /* unplug */
    uvc_usb_disconnect(&dev);
    fails += check("disconnect removes camera", camera_count() == 0 &&
                   g_uvc_n == 0);

    return fails;
}

static int test_uvc_rejects_non_uvc(void)
{
    int fails = 0;
    camera_init();
    build_desc();
    driver_usb_dev_t dev = make_usb_dev();
    /* a HID mouse config: interface class 0x03 */
    u8 hid[32];
    memset(hid, 0, sizeof(hid));
    hid[0] = 9; hid[1] = 0x02; hid[2] = 34; hid[3] = 0;
    hid[5] = 1;
    hid[9] = 9; hid[10] = 0x04; hid[12] = 0;
    hid[14] = 1; hid[15] = 0x03;
    hid[18] = 7; hid[19] = 0x05; hid[20] = 0x81; hid[21] = 0x03;
    memcpy(dev.cfg_raw, hid, 25);
    dev.cfg_len = 25;
    ctrl_reset();
    fails += check("non-UVC device refused", uvc_usb_probe(&dev) == -1);
    fails += check("count still 0", g_uvc_n == 0);
    return fails;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

#include <execinfo.h>
#include <signal.h>
static void segv_handler(int sig)
{
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "SEGV backtrace (%d frames):\n", n);
    backtrace_symbols_fd(bt, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(void)
{
    signal(SIGSEGV, segv_handler);
    int fails = 0;
    printf("== lib_mjpeg (baseline JPEG decoder) ==\n"); fflush(stdout);
    fails += test_idct();               printf("  [idct done]\n"); fflush(stdout);
    fails += test_jpeg_gray();          printf("  [gray done]\n"); fflush(stdout);
    fails += test_jpeg_444();           printf("  [444 done]\n"); fflush(stdout);
    fails += test_jpeg_420();           printf("  [420 done]\n"); fflush(stdout);
    fails += test_jpeg_dri();           printf("  [dri done]\n"); fflush(stdout);
    fails += test_jpeg_rejects();       printf("  [rej done]\n"); fflush(stdout);

    printf("== camera core (YUY2 + registry + 19 APIs) ==\n"); fflush(stdout);
    fails += test_yuy2();               printf("  [yuy2 done]\n"); fflush(stdout);
    fails += test_camera_core();        printf("  [core done]\n"); fflush(stdout);

    printf("== UVC driver (descriptors + probe/commit + capture) ==\n"); fflush(stdout);
    fails += test_uvc_parse_and_stream(); printf("  [uvc done]\n"); fflush(stdout);
    fails += test_uvc_rejects_non_uvc();  printf("  [rej2 done]\n"); fflush(stdout);

    printf("host_camera_test: %d failures - %s\n", fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails;
}
