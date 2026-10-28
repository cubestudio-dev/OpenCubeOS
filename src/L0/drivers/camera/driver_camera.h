/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/camera/driver_camera.h
 *
 * Camera subsystem core: a device registry plus the frame / format /
 * control abstractions shared by every camera backend (the in-tree
 * backend is UVC; an L1 extension can register its own camera with
 * camera_register()).
 *
 * L1 extension surface (19 interfaces, documented in
 * docs/EXTENSIONS_WP10-camera_misc.md):
 *
 *   registration        camera_register() / camera_unregister()
 *   enumeration         camera_count() / camera_get()
 *   streaming           camera_stream_start() / camera_stream_stop()
 *                       camera_stream_set_format() /
 *                       camera_stream_set_frame() /
 *                       camera_stream_set_interval()
 *   capture             camera_capture_frame() / camera_release_frame()
 *   terminal controls   camera_set_brightness() / camera_set_contrast() /
 *                       camera_set_saturation() / camera_set_exposure() /
 *                       camera_set_focus() / camera_set_zoom()
 *   capability query    camera_get_caps() / camera_get_formats() /
 *                       camera_get_frames()
 *
 * All functions return 0 on success and -1 on error unless stated
 * otherwise; they are honest about missing hardware (no camera -> -1).
 */
#ifndef OC_DRIVER_CAMERA_H
#define OC_DRIVER_CAMERA_H

#include "types.h"

/* ---- frame formats ---- */
#define CAM_FMT_MJPEG   1
#define CAM_FMT_YUY2    2
#define CAM_FMT_H264    3
#define CAM_FMT_NV12    4
#define CAM_FMT_I420    5
#define CAM_FMT_YV12    6

#define CAM_MAX_FORMATS 8
#define CAM_MAX_FRAMES  12   /* per format: distinct (w,h) frame sizes */
#define CAM_MAX_INTERVALS 8  /* per frame: distinct dwFrameIntervals   */
#define CAM_MAX_CAMERAS 4

/* control units / selectors (UVC class numbers, shared by backends) */
#define CAM_CS_SCANNING_MODE      0x00
#define CAM_CS_AE_MODE            0x01
#define CAM_CS_AE_PRIORITY        0x02
#define CAM_CS_EXPOSURE_TIME_ABS  0x03
#define CAM_CS_EXPOSURE_TIME_REL  0x04
#define CAM_CS_FOCUS_ABS          0x05
#define CAM_CS_FOCUS_AUTO         0x08
#define CAM_CS_ZOOM_ABS           0x0B
#define CAM_CS_BRIGHTNESS         0x02   /* processing unit      */
#define CAM_CS_CONTRAST           0x03
#define CAM_CS_HUE                0x06
#define CAM_CS_SATURATION         0x07
#define CAM_CS_SHARPNESS          0x08
#define CAM_CS_GAMMA              0x09
#define CAM_CS_WHITE_BALANCE      0x0A
#define CAM_CS_BACKLIGHT          0x11
#define CAM_CS_GAIN               0x12

/* capability bits (camera_get_caps) */
#define CAM_CAP_STREAMING      (1u << 0)
#define CAM_CAP_MJPEG          (1u << 1)
#define CAM_CAP_YUY2           (1u << 2)
#define CAM_CAP_H264           (1u << 3)
#define CAM_CAP_TERMINAL_CT    (1u << 4)   /* camera terminal controls */
#define CAM_CAP_TERMINAL_PU    (1u << 5)   /* processing unit controls */
#define CAM_CAP_ISO            (1u << 6)   /* isochronous transport    */
#define CAM_CAP_BULK           (1u << 7)   /* bulk transport           */

typedef struct cam_frame_desc {
    u32 w, h;
    u32 n_intervals;
    u32 intervals[CAM_MAX_INTERVALS];   /* in 100 ns units (UVC)      */
} cam_frame_desc_t;

typedef struct cam_format_desc {
    u8  fmt;                            /* CAM_FMT_*                  */
    u8  guid[16];                       /* UVC GUID (uncompressed/fb) */
    u16 n_frames;
    cam_frame_desc_t frames[CAM_MAX_FRAMES];
} cam_format_desc_t;

/* frame metadata returned by a capture */
typedef struct cam_frame_meta {
    u32 w, h;
    u8  fmt;                /* decoded pixel format: CAM_FMT_*        */
    u64 ts_ms;              /* capture timestamp (kernel ms)          */
    u32 seq;                /* driver frame counter                   */
    u32 dropped;            /* frames dropped by the ring since start */
} cam_frame_meta_t;

/* backend operations - filled in by the backend, called by the core */
typedef struct camera_dev camera_dev_t;

typedef struct camera_ops {
    /* streaming */
    int (*probe)(camera_dev_t *c, u8 fmt_idx, u8 frame_idx, u32 interval);
    int (*commit)(camera_dev_t *c);
    int (*stream_start)(camera_dev_t *c);
    int (*stream_stop)(camera_dev_t *c);
    /* capture one frame into out_rgb (decoded RGB24) */
    int (*capture)(camera_dev_t *c, u8 *out_rgb, u32 out_max,
                   cam_frame_meta_t *meta);
    /* terminal controls (unit: 0 = camera terminal, 1 = processing) */
    int (*ctrl_set)(camera_dev_t *c, u8 unit, u8 cs, i32 val);
    int (*ctrl_get)(camera_dev_t *c, u8 unit, u8 cs, i32 *val);
} camera_ops_t;

struct camera_dev {
    char name[16];                  /* "camera0", ...                */
    u8   present;
    u8   uvc_version_major, uvc_version_minor;
    u32  caps;
    const camera_ops_t *ops;
    void *priv;
    /* format table (built by the backend from the descriptors)      */
    u16 n_formats;
    cam_format_desc_t formats[CAM_MAX_FORMATS];
    /* current streaming state */
    u8   streaming;
    u16  cur_fmt;                   /* index into formats[]          */
    u16  cur_frame;                 /* index into frames[]           */
    u32  cur_interval;
    /* terminal unit ids (0 = absent) */
    u8   ct_unit, pu_unit;
    /* control ranges as reported by the device (GET_MIN/MAX/RES)    */
    i32  rng[6][3];                 /* brightness, contrast, saturation,
                                        exposure, focus, zoom:
                                        [min,max,res] per row        */
};

/* ---- subsystem ---- */
void camera_init(void);

/* ---- L1 extension interfaces ---- */
int  camera_register(camera_dev_t *cam);
int  camera_unregister(camera_dev_t *cam);
int  camera_count(void);
camera_dev_t *camera_get(int idx);

int  camera_stream_start(camera_dev_t *cam);
int  camera_stream_stop(camera_dev_t *cam);
int  camera_stream_set_format(camera_dev_t *cam, u8 fmt);       /* CAM_FMT_* */
int  camera_stream_set_frame(camera_dev_t *cam, u16 w, u16 h);
int  camera_stream_set_interval(camera_dev_t *cam, u32 interval_100ns);

int  camera_capture_frame(camera_dev_t *cam, u8 *out_rgb, u32 out_max,
                          cam_frame_meta_t *meta);
int  camera_release_frame(camera_dev_t *cam, cam_frame_meta_t *meta);

int  camera_set_brightness(camera_dev_t *cam, i32 val);
int  camera_set_contrast(camera_dev_t *cam, i32 val);
int  camera_set_saturation(camera_dev_t *cam, i32 val);
int  camera_set_exposure(camera_dev_t *cam, i32 val);
int  camera_set_focus(camera_dev_t *cam, i32 val);
int  camera_set_zoom(camera_dev_t *cam, i32 val);

u32  camera_get_caps(camera_dev_t *cam);
int  camera_get_formats(camera_dev_t *cam, u8 *out_fmts, u16 out_max);
int  camera_get_frames(camera_dev_t *cam, u8 fmt,
                       u32 *out_w, u32 *out_h, u16 out_max);

/* YUY2 (packed 4:2:2) -> RGB24, shared by the UVC backend and tests */
void camera_yuy2_to_rgb(const u8 *yuy2, u32 w, u32 h,
                        u8 *out_rgb, u32 out_max);

#endif /* OC_DRIVER_CAMERA_H */
