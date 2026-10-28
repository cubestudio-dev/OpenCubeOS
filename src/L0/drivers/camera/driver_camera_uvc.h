/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/camera/driver_camera_uvc.h
 *
 * UVC (USB Video Class) camera driver.  Sits on the WP-10d USB core
 * (driver_usb.h) as a class driver (class 0x0E video) and exposes one
 * camera_dev_t per UVC function through the camera core.
 *
 * Implemented against the "USB Device Class Definition for Video
 * Devices" (UVC 1.0/1.1/1.5):
 *   - Interface Association + VC/VS interface discovery
 *   - class-specific descriptor walk: VC_HEADER, INPUT/OUTPUT TERMINAL,
 *     CAMERA TERMINAL, PROCESSING UNIT, EXTENSION UNIT, SELECTOR UNIT,
 *     VS_INPUT_HEADER, VS_FORMAT_UNCOMPRESSED / _MJPEG / _H264 /
 *     _FRAME_BASED, VS_FRAME_*, VS_STILL_IMAGE_FRAME, VS_COLORFORMAT
 *   - version honesty: bcdUVC is parsed and reported (1.0 / 1.1 / 1.5)
 *   - probe/commit (VS_PROBE_CONTROL / VS_COMMIT_CONTROL, GET_LEN
 *     honored: 26 (1.0) / 34 (1.1) / 48 (1.5) byte layouts share the
 *     26-byte core prefix)
 *   - SET_INTERFACE(alt) streaming switch, bulk or isochronous IN
 *   - payload header parse (FID / EOF / PTS / SCR), frame reassembly,
 *     drop policy (drop-oldest on full ring)
 *   - MJPEG -> RGB via lib_mjpeg, YUY2 -> RGB via the camera core
 *
 * L1 extension surface (documented in docs/EXTENSIONS_WP10-camera_misc.md):
 *   int  driver_camera_uvc_register(void);   install the class driver
 *   int  driver_camera_uvc_count(void);      bound UVC cameras
 *   int  driver_camera_uvc_descriptors(camera_dev_t *c, u8 *out, u16 max);
 *       dump the raw class-specific descriptor bytes of the bound USB
 *       function (description/diagnostic use; 0 when absent)
 */
#ifndef OC_DRIVER_CAMERA_UVC_H
#define OC_DRIVER_CAMERA_UVC_H

#include "types.h"
#include "driver_usb.h"
#include "driver_camera.h"

/* 3.0.0-WP03 G3: no register prototype anymore - the UVC class
 * driver self-declares in the .c file (OC_DRIVER_DECL("usb", ...)). */
int  driver_camera_uvc_count(void);
int  driver_camera_uvc_descriptors(camera_dev_t *c, u8 *out, u16 max);

/* test hooks (used by uvctest and the host build): drive the probe /
 * disconnect paths against an arbitrary (possibly synthetic) device */
int  driver_camera_uvc_test_bind(driver_usb_dev_t *dev);
void driver_camera_uvc_test_disconnect(driver_usb_dev_t *dev);

#endif /* OC_DRIVER_CAMERA_UVC_H */
