/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: services/camera/cam_cmds_uvc.c
 *
 * `uvc` (status) and `uvctest` (in-kernel self test) commands.
 *
 * The self test runs the EXACT production code paths (lib_mjpeg,
 * driver_camera core, driver_camera_uvc descriptor parser) against
 * byte-exact spec fixtures: a real baseline JPEG (gray 16x16, all
 * pixels 200) and a real UVC 1.1 descriptor set (MJPEG + YUY2).  The
 * vectors live in cam_cmds_uvc_vectors.h.  Live-hardware steps
 * are skipped honestly when no camera is bound.
 */
#include "types.h"
#include "lib_string.h"
#include "lib_mjpeg.h"
#include "screen_console.h"
#include "driver_camera.h"
#include "driver_camera_uvc.h"
#include "driver_usb.h"
#include "cam_cmds_uvc.h"
#include "cam_cmds_uvc_vectors.h"

/* ------------------------------------------------------------------ */
/* output helpers                                                      */
/* ------------------------------------------------------------------ */

static void put(const char *s)
{
    screen_console_puts(s);
}

static void put_u64(u64 v)
{
    char b[24];
    u64_to_str(v, b);
    put(b);
}

/* one test step; returns 1 on failure */
static int step(const char *name, int ok)
{
    put("  ");
    put(name);
    put(": ");
    put(ok ? "PASS" : "FAIL");
    put("\n");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* `uvc` - status                                                      */
/* ------------------------------------------------------------------ */

static const char *fmt_name(u8 f)
{
    switch (f) {
    case CAM_FMT_MJPEG: return "MJPEG";
    case CAM_FMT_YUY2:  return "YUY2";
    case CAM_FMT_H264:  return "H264";
    case CAM_FMT_NV12:  return "NV12";
    case CAM_FMT_I420:  return "I420";
    case CAM_FMT_YV12:  return "YV12";
    default:            return "unknown";
    }
}

int cam_cmd_uvc(const char *args)
{
    (void)args;
    int n = camera_count();
    put("camera subsystem: ");
    put_u64((u64)n);
    put(n == 1 ? " camera\n" : " cameras\n");
    for (int i = 0; i < n; i++) {
        camera_dev_t *c = camera_get(i);
        if (!c) continue;
        put("  ");
        put(c->name);
        put(": uvc ");
        put_u64(c->uvc_version_major);
        put(".");
        put_u64(c->uvc_version_minor);
        put(" formats=");
        put_u64(c->n_formats);
        put(" streaming=");
        put(c->streaming ? "yes" : "no");
        put(" caps=0x");
        put_u64(c->caps);
        put("\n");
        for (u16 f = 0; f < c->n_formats; f++) {
            put("    fmt ");
            put(fmt_name(c->formats[f].fmt));
            put(":");
            for (u16 fr = 0; fr < c->formats[f].n_frames; fr++) {
                put(" ");
                put_u64(c->formats[f].frames[fr].w);
                put("x");
                put_u64(c->formats[f].frames[fr].h);
                put("@");
                if (c->formats[f].frames[fr].n_intervals)
                    put_u64(10000000u /
                            c->formats[f].frames[fr].intervals[0]);
                else
                    put("?");
                put("fps");
            }
            put("\n");
        }
        put("    units: camera-terminal=");
        put_u64(c->ct_unit);
        put(" processing-unit=");
        put_u64(c->pu_unit);
        put("\n");
    }
    if (n == 0)
        put("  no camera bound (UVC class driver active; QEMU has no"
            " UVC model - attach real hardware or check `usb`)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* `uvctest` - self test                                               */
/* ------------------------------------------------------------------ */

/* fake backend to exercise the camera core state machine */
static int  st_probe, st_commit, st_start, st_stop, st_cap, st_ctrl;
static int st_probe_fn(camera_dev_t *c, u8 f, u8 fr, u32 iv)
{ (void)c; st_probe = (f == 1 && fr == 1 && iv == 666666); return 0; }
static int st_commit_fn(camera_dev_t *c) { (void)c; st_commit = 1; return 0; }
static int st_start_fn(camera_dev_t *c)  { (void)c; st_start = 1; return 0; }
static int st_stop_fn(camera_dev_t *c)   { (void)c; st_stop = 1; return 0; }
static int st_cap_fn(camera_dev_t *c, u8 *o, u32 m, cam_frame_meta_t *mt)
{ (void)o; (void)m; st_cap++; if (mt) { mt->w = 16; mt->h = 16; mt->seq = 7; }
  (void)c; return 0; }
static int st_ctrl_fn(camera_dev_t *c, u8 unit, u8 cs, i32 v)
{ (void)c; (void)unit; (void)cs; st_ctrl = v; return 0; }
static int st_ctrl_get_fn(camera_dev_t *c, u8 unit, u8 cs, i32 *v)
{ (void)c; (void)unit; (void)cs; *v = 111; return 0; }

static const camera_ops_t st_ops = {
    .probe = st_probe_fn, .commit = st_commit_fn,
    .stream_start = st_start_fn, .stream_stop = st_stop_fn,
    .capture = st_cap_fn, .ctrl_set = st_ctrl_fn,
    .ctrl_get = st_ctrl_get_fn,
};

int cam_cmd_uvctest(const char *args)
{
    (void)args;
    int fails = 0;

    put("uvctest: lib_mjpeg + camera core + UVC descriptor parse\n");

    /* ---- 1. baseline JPEG decode (real vector, real decoder) ---- */
    {
        u8 rgb[16 * 16 * 3];
        u32 w = 0, h = 0;
        int rc = lib_mjpeg_decode(kvec_jpeg, KVEC_JPEG_LEN, rgb,
                                  sizeof(rgb), &w, &h, 0);
        fails += step("mjpeg decode rc", rc == 0);
        fails += step("mjpeg size 16x16", w == 16 && h == 16);
        int bad = 0;
        for (u32 i = 0; i < w * h && rc == 0; i++)
            if (rgb[i*3] != 200 || rgb[i*3+1] != 200 || rgb[i*3+2] != 200)
                bad++;
        fails += step("mjpeg pixels all 200", bad == 0);
    }

    /* ---- 2. malformed input honesty ---- */
    {
        u8 rgb[64];
        fails += step("mjpeg truncated rejected",
                      lib_mjpeg_decode(kvec_jpeg, 40, rgb, sizeof(rgb),
                                       0, 0, 0) == -1);
        fails += step("mjpeg small buffer -> -2",
                      lib_mjpeg_decode(kvec_jpeg, KVEC_JPEG_LEN, rgb,
                                       sizeof(rgb), 0, 0, 0) == -2);
    }

    /* ---- 3. YUY2 -> RGB ---- */
    {
        u8 yuy2[8] = { 200, 160, 200, 100, 200, 160, 200, 100 };
        u8 rgb[2 * 3];
        camera_yuy2_to_rgb(yuy2, 2, 1, rgb, sizeof(rgb));
        int eR = 200 + ((91881 * (100 - 128)) >> 16);
        int eG = 200 - ((22554 * (160 - 128) + 46802 * (100 - 128)) >> 16);
        int eB = 200 + ((116130 * (160 - 128)) >> 16);
        if (eR < 0) eR = 0;
        if (eR > 255) eR = 255;
        if (eG < 0) eG = 0;
        if (eG > 255) eG = 255;
        if (eB < 0) eB = 0;
        if (eB > 255) eB = 255;
        fails += step("yuy2->rgb BT.601", rgb[0] == eR && rgb[1] == eG &&
                      rgb[2] == eB);
    }

    /* ---- 4. camera core state machine ---- */
    {
        camera_dev_t cam;
        memset(&cam, 0, sizeof(cam));
        cam.ops = &st_ops;
        cam.n_formats = 1;
        cam.formats[0].fmt = CAM_FMT_MJPEG;
        cam.formats[0].n_frames = 1;
        cam.formats[0].frames[0].w = 640;
        cam.formats[0].frames[0].h = 480;
        cam.formats[0].frames[0].n_intervals = 2;
        cam.formats[0].frames[0].intervals[0] = 666666;
        cam.formats[0].frames[0].intervals[1] = 1333333;

        int rc = camera_register(&cam);
        fails += step("camera register", rc == 0 && camera_count() >= 1);
        fails += step("camera get", camera_get(camera_count() - 1) == &cam);

        fails += step("set format", camera_stream_set_format(&cam,
                      CAM_FMT_MJPEG) == 0);
        fails += step("set format unknown rejected",
                      camera_stream_set_format(&cam, CAM_FMT_NV12) == -1);
        fails += step("set frame", camera_stream_set_frame(&cam, 640,
                      480) == 0);
        fails += step("set frame mismatch rejected",
                      camera_stream_set_frame(&cam, 320, 240) == -1);
        fails += step("set interval", camera_stream_set_interval(&cam,
                      666666) == 0);
        fails += step("capture refused before start",
                      camera_capture_frame(&cam, 0, 0, 0) == -1);
        fails += step("stream start", camera_stream_start(&cam) == 0 &&
                      st_start && cam.streaming);
        cam_frame_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        fails += step("capture while streaming",
                      camera_capture_frame(&cam, 0, 0, &meta) == 0 &&
                      meta.seq == 7);
        fails += step("release frame", camera_release_frame(&cam,
                      &meta) == 0);
        fails += step("brightness step snap",
                      (cam.rng[0][0] = 0, cam.rng[0][1] = 100,
                       cam.rng[0][2] = 10, camera_set_brightness(&cam,
                       47) == 0) && st_ctrl == 40);
        fails += step("stream stop", camera_stream_stop(&cam) == 0 &&
                      st_stop && !cam.streaming);
        fails += step("camera unregister", camera_unregister(&cam) == 0);
    }

    /* ---- 5. UVC descriptor parse (real UVC 1.1 vector) ---- */
    {
        driver_usb_dev_t dev;
        memset(&dev, 0, sizeof(dev));
        dev.present = 1;
        dev.vid = 0x046D;
        dev.pid = 0x0825;
        dev.cfg_len = (u16)KVEC_DESC_LEN;
        memcpy(dev.cfg_raw, kvec_desc, KVEC_DESC_LEN);

        int before = camera_count();
        /* bind through the public USB class-driver surface: the probe
         * callback is registered under name "uvc"; drive it directly
         * through the exported test hook */
        int rc = driver_camera_uvc_test_bind(&dev);
        fails += step("uvc bind", rc == 0 && camera_count() == before + 1);
        if (rc == 0) {
            camera_dev_t *c = camera_get(camera_count() - 1);
            fails += step("uvc version 1.1",
                          c->uvc_version_major == 1 &&
                          c->uvc_version_minor == 0x10);
            fails += step("uvc 2 formats",
                          c->n_formats == 2 &&
                          c->formats[0].fmt == CAM_FMT_MJPEG &&
                          c->formats[1].fmt == CAM_FMT_YUY2);
            fails += step("uvc MJPEG 640x480",
                          c->formats[0].frames[0].w == 640 &&
                          c->formats[0].frames[0].h == 480);
            fails += step("uvc intervals 666666/1333333",
                          c->formats[0].frames[0].n_intervals == 2 &&
                          c->formats[0].frames[0].intervals[0] == 666666 &&
                          c->formats[0].frames[0].intervals[1] == 1333333);
            fails += step("uvc YUY2 320x240",
                          c->formats[1].frames[0].w == 320 &&
                          c->formats[1].frames[0].h == 240);
            fails += step("uvc terminals CT=1 PU=2",
                          c->ct_unit == 1 && c->pu_unit == 2);
            fails += step("uvc caps",
                          (c->caps & CAM_CAP_MJPEG) &&
                          (c->caps & CAM_CAP_YUY2) &&
                          (c->caps & CAM_CAP_ISO) &&
                          (c->caps & CAM_CAP_TERMINAL_PU));
            /* descriptor dump round trip */
            u8 dump[512];
            int dn = driver_camera_uvc_descriptors(c, dump, sizeof(dump));
            fails += step("uvc descriptor dump",
                          dn == (int)KVEC_DESC_LEN &&
                          memcmp(dump, kvec_desc, (usize)dn) == 0);
            driver_camera_uvc_test_disconnect(&dev);
            fails += step("uvc disconnect", camera_count() == before);
        }
    }

    /* ---- 6. live-hardware honesty ---- */
    {
        int n = camera_count();
        put("  live cameras: ");
        put_u64((u64)n);
        put(n == 0 ? " (none bound - no UVC hardware in this machine;"
                   " the tests above ran the real code paths on spec"
                   " fixtures)\n" : "\n");
        if (n > 0) {
            camera_dev_t *c = camera_get(0);
            u8 fmts[8];
            int fn = camera_get_formats(c, fmts, 8);
            put("  live formats: ");
            put_u64((u64)fn);
            put("\n");
        }
    }

    put("uvctest: ");
    put_u64((u64)fails);
    put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
    return fails == 0 ? 0 : 1;
}


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
