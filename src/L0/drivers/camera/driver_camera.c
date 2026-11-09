/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/camera/driver_camera.c
 *
 * Camera subsystem core: registry + the 19 camera_* L1 interfaces +
 * the shared YUY2 converter.  Backends (UVC in-tree, L1 extensions via
 * camera_register) fill a camera_dev_t with their format table and
 * camera_ops_t; the core does the state machine and the honest error
 * reporting.  Status printing lives in the shell layer (the `uvc`
 * command reads the registry directly) - drivers do not print.
 */
#include "types.h"
#include "lib_string.h"
#include "driver_camera.h"

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

static camera_dev_t *g_cameras[CAM_MAX_CAMERAS];
static int g_cam_n;
static int g_inited;

void camera_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    g_cam_n = 0;
}

int camera_register(camera_dev_t *cam)
{
    if (!cam || !cam->ops) return -1;
    if (g_cam_n >= CAM_MAX_CAMERAS) return -1;
    for (int i = 0; i < g_cam_n; i++)
        if (g_cameras[i] == cam) return -1;   /* double register */
    if (!cam->name[0]) {
        /* "cameraN" without a printf dependency */
        char nm[16];
        char *p = nm;
        const char *base = "camera";
        while (*base) *p++ = *base++;
        *p++ = (char)('0' + g_cam_n);
        *p = 0;
        strncpy(cam->name, nm, sizeof(cam->name) - 1);
        cam->name[sizeof(cam->name) - 1] = 0;
    }
    cam->present = 1;
    g_cameras[g_cam_n++] = cam;
    return 0;
}

int camera_unregister(camera_dev_t *cam)
{
    if (!cam) return -1;
    for (int i = 0; i < g_cam_n; i++) {
        if (g_cameras[i] == cam) {
            if (cam->streaming && cam->ops->stream_stop)
                cam->ops->stream_stop(cam);
            cam->streaming = 0;
            for (int k = i; k < g_cam_n - 1; k++)
                g_cameras[k] = g_cameras[k + 1];
            g_cam_n--;
            cam->present = 0;
            return 0;
        }
    }
    return -1;
}

int camera_count(void)      { return g_cam_n; }
camera_dev_t *camera_get(int idx)
{
    if (idx < 0 || idx >= g_cam_n) return 0;
    return g_cameras[idx];
}

/* ------------------------------------------------------------------ */
/* streaming                                                           */
/* ------------------------------------------------------------------ */

int camera_stream_start(camera_dev_t *cam)
{
    if (!cam || !cam->ops) return -1;
    if (cam->streaming) return 0;
    if (!cam->ops->stream_start) return -1;
    int rc = cam->ops->stream_start(cam);
    if (rc == 0) cam->streaming = 1;
    return rc;
}

int camera_stream_stop(camera_dev_t *cam)
{
    if (!cam || !cam->ops) return -1;
    if (!cam->streaming) return 0;
    int rc = 0;
    if (cam->ops->stream_stop) rc = cam->ops->stream_stop(cam);
    cam->streaming = 0;
    return rc;
}

/* find format by type; keeps the current frame/interval if the format
 * matches, else selects frame 0 / interval 0 */
static cam_format_desc_t *cam_find_fmt(camera_dev_t *cam, u8 fmt)
{
    for (u16 i = 0; i < cam->n_formats; i++)
        if (cam->formats[i].fmt == fmt) return &cam->formats[i];
    return 0;
}

int camera_stream_set_format(camera_dev_t *cam, u8 fmt)
{
    if (!cam) return -1;
    cam_format_desc_t *f = cam_find_fmt(cam, fmt);
    if (!f) return -1;
    cam->cur_fmt   = (u16)(f - cam->formats);
    cam->cur_frame = 0;
    if (f->n_frames > 0 && f->frames[0].n_intervals > 0)
        cam->cur_interval = f->frames[0].intervals[0];
    return 0;
}

int camera_stream_set_frame(camera_dev_t *cam, u16 w, u16 h)
{
    if (!cam || cam->cur_fmt >= cam->n_formats) return -1;
    cam_format_desc_t *f = &cam->formats[cam->cur_fmt];
    for (u16 i = 0; i < f->n_frames; i++) {
        if (f->frames[i].w == w && f->frames[i].h == h) {
            cam->cur_frame = i;
            if (f->frames[i].n_intervals > 0)
                cam->cur_interval = f->frames[i].intervals[0];
            return 0;
        }
    }
    return -1;
}

int camera_stream_set_interval(camera_dev_t *cam, u32 interval_100ns)
{
    if (!cam || cam->cur_fmt >= cam->n_formats) return -1;
    cam_format_desc_t *f = &cam->formats[cam->cur_fmt];
    if (cam->cur_frame >= f->n_frames) return -1;
    cam_frame_desc_t *fr = &f->frames[cam->cur_frame];
    for (u32 i = 0; i < fr->n_intervals; i++) {
        if (fr->intervals[i] == interval_100ns) {
            cam->cur_interval = interval_100ns;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* capture                                                             */
/* ------------------------------------------------------------------ */

int camera_capture_frame(camera_dev_t *cam, u8 *out_rgb, u32 out_max,
                         cam_frame_meta_t *meta)
{
    if (!cam || !cam->ops || !cam->ops->capture) return -1;
    if (!cam->streaming) return -1;      /* caller must start the stream */
    return cam->ops->capture(cam, out_rgb, out_max, meta);
}

int camera_release_frame(camera_dev_t *cam, cam_frame_meta_t *meta)
{
    /* the in-kernel capture path hands out plain buffers the caller
     * owns; release() is the symmetric ownership hook.  It validates
     * its arguments so an L1 backend with a real ring can hook it. */
    if (!cam || !meta) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* terminal controls                                                   */
/* ------------------------------------------------------------------ */

/* row order in cam->rng[][] */
#define RNG_BRIGHTNESS 0
#define RNG_CONTRAST   1
#define RNG_SATURATION 2
#define RNG_EXPOSURE   3
#define RNG_FOCUS      4
#define RNG_ZOOM       5

static int cam_ctrl_set(camera_dev_t *cam, int rng_row, u8 unit, u8 cs,
                        i32 val)
{
    if (!cam || !cam->ops || !cam->ops->ctrl_set) return -1;
    /* clamp into the device-reported range when it is known */
    if (cam->rng[rng_row][0] != 0 || cam->rng[rng_row][1] != 0) {
        i32 mn = cam->rng[rng_row][0], mx = cam->rng[rng_row][1];
        i32 step = cam->rng[rng_row][2] ? cam->rng[rng_row][2] : 1;
        if (val < mn) val = mn;
        if (val > mx) val = mx;
        val = mn + ((val - mn) / step) * step;
    }
    return cam->ops->ctrl_set(cam, unit, cs, val);
}

int camera_set_brightness(camera_dev_t *cam, i32 val)
{ return cam_ctrl_set(cam, RNG_BRIGHTNESS, 1, CAM_CS_BRIGHTNESS, val); }
int camera_set_contrast(camera_dev_t *cam, i32 val)
{ return cam_ctrl_set(cam, RNG_CONTRAST, 1, CAM_CS_CONTRAST, val); }
int camera_set_saturation(camera_dev_t *cam, i32 val)
{ return cam_ctrl_set(cam, RNG_SATURATION, 1, CAM_CS_SATURATION, val); }
int camera_set_exposure(camera_dev_t *cam, i32 val)
{ return cam_ctrl_set(cam, RNG_EXPOSURE, 0, CAM_CS_EXPOSURE_TIME_ABS, val); }
int camera_set_focus(camera_dev_t *cam, i32 val)
{ return cam_ctrl_set(cam, RNG_FOCUS, 0, CAM_CS_FOCUS_ABS, val); }
int camera_set_zoom(camera_dev_t *cam, i32 val)
{ return cam_ctrl_set(cam, RNG_ZOOM, 0, CAM_CS_ZOOM_ABS, val); }

/* ------------------------------------------------------------------ */
/* capability queries                                                  */
/* ------------------------------------------------------------------ */

u32 camera_get_caps(camera_dev_t *cam)
{
    if (!cam) return 0;
    return cam->caps;
}

int camera_get_formats(camera_dev_t *cam, u8 *out_fmts, u16 out_max)
{
    if (!cam || !out_fmts) return -1;
    u16 n = cam->n_formats;
    if (n > out_max) n = out_max;
    for (u16 i = 0; i < n; i++) out_fmts[i] = cam->formats[i].fmt;
    return n;
}

int camera_get_frames(camera_dev_t *cam, u8 fmt,
                      u32 *out_w, u32 *out_h, u16 out_max)
{
    if (!cam || !out_w || !out_h) return -1;
    cam_format_desc_t *f = cam_find_fmt(cam, fmt);
    if (!f) return -1;
    u16 n = f->n_frames;
    if (n > out_max) n = out_max;
    for (u16 i = 0; i < n; i++) {
        out_w[i] = f->frames[i].w;
        out_h[i] = f->frames[i].h;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* YUY2 (packed 4:2:2) -> RGB24                                        */
/* ------------------------------------------------------------------ */

static inline u8 cam_clamp8(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (u8)v;
}

void camera_yuy2_to_rgb(const u8 *yuy2, u32 w, u32 h,
                        u8 *out_rgb, u32 out_max)
{
    if (!yuy2 || !out_rgb) return;
    if (w * h * 3u > out_max) return;
    /* BT.601: R = Y + 1.402(Cr-128), G = Y - 0.344(Cb-128) - 0.714(Cr-128),
     * B = Y + 1.772(Cb-128); two Y samples share one Cb/Cr pair. */
    for (u32 y = 0; y < h; y++) {
        const u8 *src = yuy2 + y * w * 2u;
        u8 *dst = out_rgb + y * w * 3u;
        for (u32 x = 0; x + 1 < w; x += 2) {
            int y0 = src[0], cb = src[1], y1 = src[2], cr = src[3];
            src += 4;
            cb -= 128; cr -= 128;
            dst[0] = cam_clamp8(y0 + ((91881 * cr) >> 16));
            dst[1] = cam_clamp8(y0 - ((22554 * cb + 46802 * cr) >> 16));
            dst[2] = cam_clamp8(y0 + ((116130 * cb) >> 16));
            dst[3] = cam_clamp8(y1 + ((91881 * cr) >> 16));
            dst[4] = cam_clamp8(y1 - ((22554 * cb + 46802 * cr) >> 16));
            dst[5] = cam_clamp8(y1 + ((116130 * cb) >> 16));
            dst += 6;
        }
        if (w & 1) {          /* odd width: last pixel uses Y + current cb */
            int y0 = src[0], cbv = src[1] - 128, crv = src[3] - 128;
            dst[0] = cam_clamp8(y0 + ((91881 * crv) >> 16));
            dst[1] = cam_clamp8(y0 - ((22554 * cbv + 46802 * crv) >> 16));
            dst[2] = cam_clamp8(y0 + ((116130 * cbv) >> 16));
        }
    }
}
