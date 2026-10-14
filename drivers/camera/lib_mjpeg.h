/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/camera/lib_mjpeg.h
 *
 * Baseline JPEG decoder used by the UVC camera driver to turn MJPEG
 * payloads into RGB frames.  This is a REAL decoder, not a stub:
 *
 *   - full JPEG syntax walker: SOI/APPn/COM/DQT/DHT/SOF0/SOF1/SOS/DRI/
 *     RSTn/EOI, bounds-checked at every step
 *   - baseline sequential DCT, Huffman entropy coding (the MJPEG
 *     payloads produced by real UVC cameras are baseline JPEG)
 *   - progressive JPEG (SOF2) is detected and REFUSED with a clear
 *     error - it is explicitly optional in the work package and the
 *     decoder never pretends to have decoded it
 *   - integer (fixed-point) IDCT: the kernel is freestanding with
 *     -mno-sse, so no floating point is used at run time; the Q13
 *     cosine matrix is validated against a double-precision reference
 *     in tests/host_camera_test.c (max error <= 1 LSB)
 *   - chroma upsampling: 4:4:4, 4:2:2 (h2v1), 4:2:0 (h2v2), grayscale
 *   - BT.601 YCbCr -> RGB conversion (libjpeg-style integer math)
 *
 * L1 extension surface (documented in docs/EXTENSIONS_WP10-camera_misc.md):
 *
 *   int lib_mjpeg_decode(const u8 *jpeg, u32 len, u8 *out_rgb, u32 out_max,
 *                        u32 *out_w, u32 *out_h, u32 *out_stride);
 *       Decode one baseline JPEG image into packed RGB24 (out_stride =
 *       3*out_w).  Returns 0 on success (out_w/out_h filled, out_stride
 *       filled when non-NULL), -1 on a malformed/unsupported stream
 *       (including progressive), -2 when out_max is too small.  The
 *       caller supplies the output buffer.
 *
 *   const char *lib_mjpeg_last_error(void);
 *       Human-readable (ASCII) reason for the last decode failure -
 *       used by the uvctest command to report honest diagnostics.
 */
#ifndef OC_LIB_MJPEG_H
#define OC_LIB_MJPEG_H

#include "types.h"

int  lib_mjpeg_decode(const u8 *jpeg, u32 len, u8 *out_rgb, u32 out_max,
                      u32 *out_w, u32 *out_h, u32 *out_stride);
const char *lib_mjpeg_last_error(void);

#endif /* OC_LIB_MJPEG_H */
