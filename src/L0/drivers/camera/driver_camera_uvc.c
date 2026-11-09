/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/camera/driver_camera_uvc.c
 *
 * UVC camera class driver.  See driver_camera_uvc.h for the contract.
 *
 * Descriptor parsing walks the device's cached raw configuration
 * descriptor (driver_usb_dev_t.cfg_raw, filled by the USB core during
 * enumeration) - the same bytes the USB core got with GET_DESCRIPTOR,
 * so no additional control traffic is needed to parse the topology.
 * Runtime streaming control/capture uses the USB core transfer API.
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "driver_usb.h"
#include "core_timer.h"
#include "core_registry.h"
#include "lib_mjpeg.h"
#include "driver_camera.h"
#include "driver_camera_uvc.h"

/* ---- UVC constants (USB Video Class spec) ---- */
#define UVC_CS_INTERFACE   0x24
#define UVC_CS_ENDPOINT    0x25

/* VC interface subtypes */
#define VC_DESCRIPTOR_SUBTYPE_UNDEFINED        0x00
#define VC_DESCRIPTOR_SUBTYPE_HEADER           0x01
#define VC_DESCRIPTOR_SUBTYPE_INPUT_TERMINAL   0x02
#define VC_DESCRIPTOR_SUBTYPE_OUTPUT_TERMINAL  0x03
#define VC_DESCRIPTOR_SUBTYPE_SELECTOR_UNIT    0x04
#define VC_DESCRIPTOR_SUBTYPE_PROCESSING_UNIT  0x05
#define VC_DESCRIPTOR_SUBTYPE_EXTENSION_UNIT   0x06
#define VC_DESCRIPTOR_SUBTYPE_CLOCK_SOURCE     0x0C   /* UVC 1.5 */

/* VS interface subtypes */
#define VS_DESCRIPTOR_SUBTYPE_INPUT_HEADER     0x01
#define VS_DESCRIPTOR_SUBTYPE_OUTPUT_HEADER    0x02
#define VS_DESCRIPTOR_SUBTYPE_STILL_FRAME      0x03
#define VS_DESCRIPTOR_SUBTYPE_FORMAT_UNCOMP    0x04
#define VS_DESCRIPTOR_SUBTYPE_FRAME_UNCOMP     0x05
#define VS_DESCRIPTOR_SUBTYPE_FORMAT_MJPEG     0x07
#define VS_DESCRIPTOR_SUBTYPE_FRAME_MJPEG      0x08
#define VS_DESCRIPTOR_SUBTYPE_FORMAT_FRAMEBASE 0x10
#define VS_DESCRIPTOR_SUBTYPE_FRAME_FRAMEBASE  0x11
#define VS_DESCRIPTOR_SUBTYPE_FORMAT_H264      0x0A   /* UVC 1.5 */
#define VS_DESCRIPTOR_SUBTYPE_FRAME_H264       0x0B
#define VS_DESCRIPTOR_SUBTYPE_COLORFORMAT      0x0D

/* VC control selectors (wValue high byte) */
#define VC_REQ_SET_CUR   0x01
#define VC_REQ_GET_CUR   0x81
#define VC_REQ_GET_MIN   0x82
#define VC_REQ_GET_MAX   0x83
#define VC_REQ_GET_RES   0x84
#define VC_REQ_GET_LEN   0x85
#define VC_REQ_GET_INFO  0x86
#define VC_REQ_GET_DEF   0x87

/* VS control selectors */
#define VS_CS_PROBE_CONTROL   0x01
#define VS_CS_COMMIT_CONTROL  0x02

/* payload header bits (bmHeaderInfo, UVC 1.5 Table 2-5):
 * D0 = FID (frame identifier toggle), D1 = EOF (end of frame),
 * D2 = PTS present, D3 = SCR present. */
#define UVC_HDR_FID_BIT 0x01
#define UVC_HDR_EOF_BIT 0x02
#define UVC_HDR_PTS_BIT 0x04
#define UVC_HDR_SCR_BIT 0x08

/* probe/commit core layout (26-byte 1.0 prefix, shared by 1.1/1.5) */
typedef struct uvc_probe {
    u16 bmHint;
    u8  bFormatIndex;
    u8  bFrameIndex;
    u32 dwFrameInterval;
    u16 wKeyFrameRate;
    u16 wPFrameRate;
    u16 wCompQuality;
    u16 wCompWindowSize;
    u16 wDelay;
    u32 dwMaxVideoFrameSize;
    u32 dwMaxPayloadTransferSize;
    /* 1.1+ */
    u32 dwClockFrequency;
    u8  bmFramingInfo;
    u8  bPreferredVersion;
    u8  bMinVersion;
    u8  bMaxVersion;
    u32 len;            /* control length as reported by GET_LEN      */
} uvc_probe_t;

/* per-camera UVC private state */
typedef struct uvc_cam {
    camera_dev_t   cam;          /* public face (must be first)        */
    driver_usb_dev_t *dev;       /* USB device                         */
    u8  vc_if, vs_if;            /* interface numbers                  */
    u8  vc_in_ep;                /* status interrupt EP (0 = none)     */
    u8  stream_ep;               /* streaming IN EP address            */
    u8  stream_ep_attr;          /* USB_EP_ATTR_ISO / _BULK            */
    u16 stream_ep_mps;
    u8  alt0, alt_stream;        /* alternate settings                 */
    /* reassembly ring (2 frames max, drop-oldest) */
    u8  *ring[2];
    u32 ring_len[2];
    u32 ring_fill;
    u32 ring_head;               /* next completed frame slot          */
    u8  cur_fid;                 /* current FID state                  */
    u8  have_fid;
    u32 seq, dropped;
    uvc_probe_t probe;
} uvc_cam_t;

static uvc_cam_t *g_uvc_cams[4];
static int g_uvc_n;

int driver_camera_uvc_count(void) { return g_uvc_n; }

/* ------------------------------------------------------------------ */
/* little-endian helpers                                               */
/* ------------------------------------------------------------------ */

static inline u16 le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* descriptor parsing                                                  */
/* ------------------------------------------------------------------ */

/* GUID -> CAM_FMT_* (uncompressed/frame-based GUIDs are the ASCII
 * signature backwards; H264/MJPEG handled by subtype separately) */
static u8 uvc_guid_to_fmt(const u8 *guid)
{
    /* "YUY2" = 59 55 59 32 (LE first 4 bytes) */
    if (memcmp(guid, "YUY2", 4) == 0) return CAM_FMT_YUY2;
    if (memcmp(guid, "NV12", 4) == 0) return CAM_FMT_NV12;
    if (memcmp(guid, "I420", 4) == 0) return CAM_FMT_I420;
    if (memcmp(guid, "YV12", 4) == 0) return CAM_FMT_YV12;
    if (memcmp(guid, "H264", 4) == 0) return CAM_FMT_H264;
    return 0;
}

/* Walk one VS interface's class-specific descriptors into a format
 * table slot.  Returns the number of formats parsed (0 on parse
 * problems - the camera still appears with what was parsed). */
static int uvc_parse_vs(camera_dev_t *cam, const u8 *d, u32 len,
                        u32 vs_start, u32 vs_end)
{
    u32 p = vs_start;
    int cur_fmt = -1;
    int n_fmt = 0;
    int max_fmt = (int)sizeof(cam->formats) / sizeof(cam->formats[0]);

    while (p + 2 <= vs_end && p + 2 <= len) {
        u8 bLength = d[p];
        if (bLength < 3 || p + bLength > vs_end || p + bLength > len) break;
        u8 bType = d[p + 1];
        u8 bSub  = d[p + 2];
        if (bType != UVC_CS_INTERFACE) { p += bLength; continue; }

        switch (bSub) {
        case VS_DESCRIPTOR_SUBTYPE_INPUT_HEADER: {
            if (bLength < 13) break;
            /* bNumFormats at offset 3 */
            break;
        }
        case VS_DESCRIPTOR_SUBTYPE_FORMAT_UNCOMP:
        case VS_DESCRIPTOR_SUBTYPE_FORMAT_MJPEG:
        case VS_DESCRIPTOR_SUBTYPE_FORMAT_H264:
        case VS_DESCRIPTOR_SUBTYPE_FORMAT_FRAMEBASE: {
            if (n_fmt >= max_fmt) break;
            cam_format_desc_t *f = &cam->formats[n_fmt];
            memset(f, 0, sizeof(*f));
            switch (bSub) {
            case VS_DESCRIPTOR_SUBTYPE_FORMAT_MJPEG:
                f->fmt = CAM_FMT_MJPEG; break;
            case VS_DESCRIPTOR_SUBTYPE_FORMAT_H264:
                f->fmt = CAM_FMT_H264; break;
            default: {
                u8 g = 0;
                if (bLength >= 20) g = uvc_guid_to_fmt(d + p + 5);
                f->fmt = g;
                if (bLength >= 24) memcpy(f->guid, d + p + 5, 16);
                break;
            }
            }
            f->fmt = f->fmt ? f->fmt : CAM_FMT_MJPEG;   /* unknown GUID: MJPEG-ish */
            cur_fmt = n_fmt++;
            break;
        }
        case VS_DESCRIPTOR_SUBTYPE_FRAME_UNCOMP:
        case VS_DESCRIPTOR_SUBTYPE_FRAME_MJPEG:
        case VS_DESCRIPTOR_SUBTYPE_FRAME_H264:
        case VS_DESCRIPTOR_SUBTYPE_FRAME_FRAMEBASE: {
            if (cur_fmt < 0 || bLength < 26) break;
            cam_format_desc_t *f = &cam->formats[cur_fmt];
            if (f->n_frames >= CAM_MAX_FRAMES) break;
            cam_frame_desc_t *fr = &f->frames[f->n_frames++];
            /* layout: bFrameIndex(3) bmCaps(4) wWidth(5) wHeight(7)
             * dwMinBitRate(9) dwMaxBitRate(13)
             * dwMaxVideoFrameBufferSize(17) dwDefaultFrameInterval(21)
             * bFrameIntervalType(25) dwFrameInterval[](26) */
            fr->w = le16(d + p + 5);
            fr->h = le16(d + p + 7);
            u8 itype = (bLength > 25) ? d[p + 25] : 0;
            u32 n_iv = 0;
            if (itype == 0) {
                /* continuous: min/max/step at 26/30/34 */
                if (bLength >= 38) {
                    u32 mn = le32(d + p + 26), mx = le32(d + p + 30);
                    u32 st = le32(d + p + 34) ? le32(d + p + 34) : 1;
                    if (mx < mn) { u32 t = mn; mn = mx; mx = t; }
                    for (u32 iv = mn; iv <= mx && n_iv < CAM_MAX_INTERVALS;
                         iv += st)
                        fr->intervals[n_iv++] = iv;
                }
            } else {
                u32 want = itype;
                if (bLength >= 26 + 4 * want) want = (bLength - 26) / 4;
                for (u32 i = 0; i < want && n_iv < CAM_MAX_INTERVALS; i++)
                    fr->intervals[n_iv++] = le32(d + p + 26 + 4 * i);
            }
            fr->n_intervals = n_iv;
            break;
        }
        default:
            break;      /* still image / color matching / etc. */
        }
        p += bLength;
    }
    cam->n_formats = (u16)n_fmt;
    return n_fmt;
}

/* Parse the VC interface (terminals + units).  Returns 0/1: has PU. */
static int uvc_parse_vc(camera_dev_t *cam, const u8 *d, u32 len,
                        u32 vc_start, u32 vc_end)
{
    u32 p = vc_start;
    int has_pu = 0;
    cam->ct_unit = 0;
    cam->pu_unit = 0;

    while (p + 2 <= vc_end && p + 2 <= len) {
        u8 bLength = d[p];
        if (bLength < 3 || p + bLength > vc_end || p + bLength > len) break;
        u8 bType = d[p + 1];
        u8 bSub  = d[p + 2];
        if (bType == UVC_CS_INTERFACE) {
            switch (bSub) {
            case VC_DESCRIPTOR_SUBTYPE_INPUT_TERMINAL:
                /* wTerminalType 0x0201 = Camera Terminal */
                if (bLength >= 8 && le16(d + p + 4) == 0x0201)
                    cam->ct_unit = d[p + 3];
                break;
            case VC_DESCRIPTOR_SUBTYPE_PROCESSING_UNIT:
                if (bLength >= 9) { cam->pu_unit = d[p + 3]; has_pu = 1; }
                break;
            default:
                break;
            }
        }
        p += bLength;
    }
    return has_pu;
}

/* Full descriptor walk for one enumerated USB device that looks like a
 * UVC function.  Returns 0 and fills cam on success, -1 if this device
 * is not a UVC camera. */
static int uvc_parse_device(uvc_cam_t *uc)
{
    camera_dev_t *cam = &uc->cam;
    driver_usb_dev_t *dev = uc->dev;
    const u8 *d = dev->cfg_raw;
    u32 len = dev->cfg_len;
    u32 p = 0;

    int vc_if = -1, vs_if = -1;
    u32 vc_start = 0, vc_end = 0, vs_start = 0, vs_end = 0;

    while (p + 2 <= len) {
        u8 bLength = d[p];
        if (bLength < 2 || p + bLength > len) break;
        u8 bType = d[p + 1];

        if (bType == USB_DT_INTERFACE) {
            if (bLength < 9) { p += bLength; continue; }
            const u8 *ifd = d + p;
            u8 iclass = ifd[5], isub = ifd[6];
            if (iclass == 0x0E && isub == 0x01) {         /* VC */
                vc_if = ifd[2];
                vc_start = p + bLength;                  /* CS after */
            } else if (iclass == 0x0E && isub == 0x02) { /* VS */
                /* a VS interface ENDS the VC segment (and any earlier
                 * VS segment): the VC class subtypes 0x02..0x06 overlap
                 * the VS subtypes, so the boundaries must be exact */
                if (vc_if >= 0 && vc_start && !vc_end) vc_end = p;
                if (vs_if >= 0 && vs_start && !vs_end)
                    vs_end = p;                          /* close prev */
                if (vs_if < 0) {
                    vs_if = ifd[2];
                    vs_start = p + bLength;
                }
            } else {
                if (vs_if >= 0 && vs_start && !vs_end) vs_end = p;
                if (vc_if >= 0 && vc_start && !vc_end) vc_end = p;
            }
        } else if (bType == USB_DT_ENDPOINT) {
            if (bLength >= 7) {
                const u8 *ep = d + p;
                u8 attr = ep[3] & 3;
                u8 addr = ep[2];
                if (vs_if >= 0 && (addr & 0x80) &&
                    (attr == USB_EP_ATTR_ISO || attr == USB_EP_ATTR_BULK)) {
                    uc->stream_ep     = addr;
                    uc->stream_ep_attr = attr;
                    uc->stream_ep_mps = le16(ep + 4);
                } else if (vc_if >= 0 && (addr & 0x80) &&
                           attr == USB_EP_ATTR_INTERRUPT) {
                    uc->vc_in_ep = addr;
                }
            }
        }
        p += bLength;
    }
    if (vs_if >= 0 && vs_start && !vs_end) vs_end = p;
    if (vc_if >= 0 && vc_start && !vc_end) vc_end = p;

    if (vc_if < 0 || vs_if < 0) return -1;

    uc->vc_if = (u8)vc_if;
    uc->vs_if = (u8)vs_if;

    /* UVC version: read from the VC_HEADER (bcdUVC at offset 3 of the
     * class-specific header, which sits right after the interface
     * descriptor in vc_start) */
    cam->uvc_version_major = 1;
    cam->uvc_version_minor = 0;
    if (vc_start + 5 <= len && d[vc_start + 1] == UVC_CS_INTERFACE &&
        d[vc_start + 2] == VC_DESCRIPTOR_SUBTYPE_HEADER) {
        u16 bcd = le16(d + vc_start + 3);
        /* bcdUVC 0x0100/0x0110/0x0150 -> major 1, minor keeps the raw
         * BCD nibble (1.0 / 1.10 / 1.50 as the spec prints them) */
        cam->uvc_version_major = (u8)(bcd >> 8);
        cam->uvc_version_minor = (u8)(bcd & 0xFF);
    }

    uvc_parse_vc(cam, d, len, vc_start, vc_end);
    uvc_parse_vs(cam, d, len, vs_start, vs_end);
    return (cam->n_formats > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* control transfer helpers                                            */
/* ------------------------------------------------------------------ */

/* class request to the VC/VS interface: bmRequestType 0x21 (out) /
 * 0xA1 (in), wIndex = (interface << 8) | unit */
static int uvc_class_in(uvc_cam_t *uc, u8 cs, u8 unit_if, u8 req,
                        void *buf, u16 len)
{
    driver_usb_setup_t s;
    s.bmRequestType = 0xA1;              /* interface IN, class */
    s.bRequest = req;
    s.wValue = (u16)(cs << 8);
    s.wIndex = (u16)((uc->vs_if << 8) | unit_if);
    s.wLength = len;
    return driver_usb_control_transfer(uc->dev, &s, buf, len);
}

static int uvc_class_out(uvc_cam_t *uc, u8 cs, u8 unit_if, u8 req,
                         const void *buf, u16 len)
{
    driver_usb_setup_t s;
    s.bmRequestType = 0x21;              /* interface OUT, class */
    s.bRequest = req;
    s.wValue = (u16)(cs << 8);
    s.wIndex = (u16)((uc->vs_if << 8) | unit_if);
    s.wLength = len;
    return driver_usb_control_transfer(uc->dev, &s, (void *)buf, len);
}

/* probe/commit marshalling (26-byte core + version extras) */
static void uvc_pack_probe(const uvc_probe_t *p, u8 *b, u32 len)
{
    memset(b, 0, len);
    if (len > 26) len = 26;
    b[0]  = (u8)(p->bmHint & 0xFF);
    b[1]  = (u8)(p->bmHint >> 8);
    b[2]  = p->bFormatIndex;
    b[3]  = p->bFrameIndex;
    b[4]  = (u8)(p->dwFrameInterval);
    b[5]  = (u8)(p->dwFrameInterval >> 8);
    b[6]  = (u8)(p->dwFrameInterval >> 16);
    b[7]  = (u8)(p->dwFrameInterval >> 24);
    b[8]  = (u8)(p->wKeyFrameRate);  b[9]  = (u8)(p->wKeyFrameRate >> 8);
    b[10] = (u8)(p->wPFrameRate);    b[11] = (u8)(p->wPFrameRate >> 8);
    b[12] = (u8)(p->wCompQuality);   b[13] = (u8)(p->wCompQuality >> 8);
    b[14] = (u8)(p->wCompWindowSize);b[15] = (u8)(p->wCompWindowSize >> 8);
    b[16] = (u8)(p->wDelay);         b[17] = (u8)(p->wDelay >> 8);
    b[18] = (u8)(p->dwMaxVideoFrameSize);
    b[19] = (u8)(p->dwMaxVideoFrameSize >> 8);
    b[20] = (u8)(p->dwMaxVideoFrameSize >> 16);
    b[21] = (u8)(p->dwMaxVideoFrameSize >> 24);
    b[22] = (u8)(p->dwMaxPayloadTransferSize);
    b[23] = (u8)(p->dwMaxPayloadTransferSize >> 8);
    b[24] = (u8)(p->dwMaxPayloadTransferSize >> 16);
    b[25] = (u8)(p->dwMaxPayloadTransferSize >> 24);
}

static void uvc_unpack_probe(uvc_probe_t *p, const u8 *b, u32 len)
{
    memset(p, 0, sizeof(*p));
    p->len = len;
    if (len > 26) len = 26;
    if (len < 26) return;
    p->bmHint = le16(b + 0);
    p->bFormatIndex  = b[2];
    p->bFrameIndex   = b[3];
    p->dwFrameInterval = le32(b + 4);
    p->wKeyFrameRate = le16(b + 8);
    p->wPFrameRate   = le16(b + 10);
    p->wCompQuality  = le16(b + 12);
    p->wCompWindowSize = le16(b + 14);
    p->wDelay        = le16(b + 16);
    p->dwMaxVideoFrameSize = le32(b + 18);
    p->dwMaxPayloadTransferSize = le32(b + 22);
    if (p->len >= 34) {
        p->dwClockFrequency = le32(b + 26);
        p->bmFramingInfo    = b[30];
        p->bPreferredVersion = b[31];
        p->bMinVersion      = b[32];
        p->bMaxVersion      = b[33];
    }
    /* 1.5 adds bmLayoutPerStream etc. at 34..47 - length-honoring,
     * the core fields above are identical in all versions. */
}

/* ------------------------------------------------------------------ */
/* streaming                                                           */
/* ------------------------------------------------------------------ */

static int uvc_probe_ctrl(camera_dev_t *c, u8 fmt_idx, u8 frame_idx,
                          u32 interval)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    u8 b[48];
    uvc_probe_t *p = &uc->probe;
    memset(p, 0, sizeof(*p));
    p->bFormatIndex = fmt_idx;
    p->bFrameIndex  = frame_idx;
    p->dwFrameInterval = interval;
    uvc_pack_probe(p, b, 26);

    /* learn the control's length first (26/34/48) */
    u8 lenbuf[2];
    int rc = uvc_class_in(uc, VS_CS_PROBE_CONTROL, 0, VC_REQ_GET_LEN,
                          lenbuf, 2);
    p->len = (rc == 0) ? le16(lenbuf) : 26;
    if (p->len < 26 || p->len > 48) p->len = 26;

    rc = uvc_class_out(uc, VS_CS_PROBE_CONTROL, 0, VC_REQ_SET_CUR,
                       b, 26);
    if (rc != 0) return rc;
    memset(b, 0, sizeof(b));
    rc = uvc_class_in(uc, VS_CS_PROBE_CONTROL, 0, VC_REQ_GET_CUR,
                      b, p->len);
    if (rc != 0) return rc;
    uvc_unpack_probe(p, b, p->len);
    return 0;
}

static int uvc_commit_ctrl(camera_dev_t *c)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    u8 b[48];
    uvc_pack_probe(&uc->probe, b, 26);
    return uvc_class_out(uc, VS_CS_COMMIT_CONTROL, 0, VC_REQ_SET_CUR,
                         b, 26);
}

static int uvc_stream_start(camera_dev_t *c)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    /* switch the VS interface to the streaming alternate setting */
    int rc = driver_usb_set_interface(uc->dev, uc->vs_if, uc->alt_stream);
    if (rc != 0) return rc;
    uc->ring_head = 0;
    uc->ring_fill = 0;
    uc->have_fid = 0;
    return 0;
}

static int uvc_stream_stop(camera_dev_t *c)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    return driver_usb_set_interface(uc->dev, uc->vs_if, uc->alt0);
}

/* real capture used by camera_ops.capture */
static int uvc_capture_frame(camera_dev_t *c, u8 *out_rgb, u32 out_max,
                             cam_frame_meta_t *meta)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    if (!uc->dev || !uc->stream_ep) return -1;

    u8 pkt[1024];
    u32 guard = 0;

    for (;;) {
        if (guard++ > 512) return -1;

        int n;
        if (uc->stream_ep_attr == USB_EP_ATTR_ISO)
            n = driver_usb_isochronous_transfer(uc->dev, uc->stream_ep,
                                                pkt,
                                                (u16)sizeof(pkt));
        else
            n = driver_usb_bulk_transfer(uc->dev, uc->stream_ep, pkt,
                                         (u16)(uc->stream_ep_mps < sizeof(pkt)
                                                ? uc->stream_ep_mps
                                                : sizeof(pkt)));
        if (n == OC_USB_ETIMEDOUT || n == OC_USB_ENAK) return -1;
        if (n < 0) return -1;

        /* payload header + data */
        u32 plen = (u32)n;
        if (plen < 2) continue;
        u8 bLength = pkt[0];
        if (bLength < 2 || bLength > 12 || bLength > plen) continue;
        u8 bm = pkt[1];
        const u8 *data = pkt + bLength;
        u32 dlen = plen - bLength;

        u8 fid = (bm & UVC_HDR_FID_BIT) ? 1 : 0;
        int eof = (bm & UVC_HDR_EOF_BIT) ? 1 : 0;

        if (uc->have_fid && fid != uc->cur_fid) {
            /* new frame starts: discard any partial one */
            uc->ring_fill = 0;
            uc->dropped++;
        }
        uc->cur_fid = fid;
        uc->have_fid = 1;

        if (dlen) {
            if (uc->ring[0]) {
                u32 space = uc->ring_len[0] > uc->ring_fill
                            ? uc->ring_len[0] - uc->ring_fill : 0;
                u32 copy = dlen < space ? dlen : space;
                memcpy(uc->ring[0] + uc->ring_fill, data, copy);
                uc->ring_fill += copy;
                if (copy < dlen) uc->dropped++;
            }
        }

        if (eof && uc->ring_fill > 0) {
            /* complete frame in ring[0]/ring_len[0] -> decode */
            u32 got = uc->ring_fill;
            uc->ring_fill = 0;
            uc->seq++;

            cam_format_desc_t *f = (c->cur_fmt < c->n_formats)
                                   ? &c->formats[c->cur_fmt] : 0;
            u8 fmt = f ? f->fmt : CAM_FMT_MJPEG;

            if (meta) {
                meta->w = f && c->cur_frame < f->n_frames
                          ? f->frames[c->cur_frame].w : 0;
                meta->h = f && c->cur_frame < f->n_frames
                          ? f->frames[c->cur_frame].h : 0;
                meta->fmt = CAM_FMT_MJPEG;   /* decoded output is RGB; the
                                                source format is fmt     */
                meta->ts_ms = core_timer_now_ms();
                meta->seq = uc->seq;
                meta->dropped = uc->dropped;
            }
            (void)fmt;

            if (!out_rgb) return 0;          /* metadata-only capture   */
            if (fmt == CAM_FMT_YUY2) {
                u32 fw = f && c->cur_frame < f->n_frames
                         ? f->frames[c->cur_frame].w : 0;
                u32 fh = f && c->cur_frame < f->n_frames
                         ? f->frames[c->cur_frame].h : 0;
                camera_yuy2_to_rgb(uc->ring[0], fw, fh, out_rgb, out_max);
                if (meta) { meta->w = fw; meta->h = fh; meta->fmt = CAM_FMT_YUY2; }
                return 0;
            }
            /* MJPEG (default) */
            u32 w = 0, h = 0;
            int rc = lib_mjpeg_decode(uc->ring[0], got, out_rgb, out_max,
                                      &w, &h, 0);
            if (rc != 0) return -1;
            if (meta) { meta->w = w; meta->h = h; }
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* terminal controls                                                   */
/* ------------------------------------------------------------------ */

static int uvc_ctrl_set(camera_dev_t *c, u8 unit, u8 cs, i32 val)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    u8 ifno = unit ? c->pu_unit : c->ct_unit;
    if (!ifno) return -1;
    /* controls are signed 2-byte (most) or 4-byte (exposure); 2-byte
     * covers brightness/contrast/saturation/focus/zoom here */
    u8 b[4];
    b[0] = (u8)(val & 0xFF);
    b[1] = (u8)((val >> 8) & 0xFF);
    b[2] = 0; b[3] = 0;
    /* unit controls address the VC interface:
     * wIndex = (unit << 8) | vc_if */
    driver_usb_setup_t s;
    s.bmRequestType = 0x21;
    s.bRequest = VC_REQ_SET_CUR;
    s.wValue = (u16)(cs << 8);
    s.wIndex = (u16)((ifno << 8) | uc->vc_if);
    s.wLength = 2;
    return driver_usb_control_transfer(uc->dev, &s, b, 2);
}

static int uvc_ctrl_get(camera_dev_t *c, u8 unit, u8 cs, i32 *val)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    u8 ifno = unit ? c->pu_unit : c->ct_unit;
    if (!ifno || !val) return -1;
    u8 b[4];
    memset(b, 0, sizeof(b));
    driver_usb_setup_t s;
    s.bmRequestType = 0xA1;
    s.bRequest = VC_REQ_GET_CUR;
    s.wValue = (u16)(cs << 8);
    s.wIndex = (u16)((ifno << 8) | uc->vc_if);
    s.wLength = 2;
    int rc = driver_usb_control_transfer(uc->dev, &s, b, 2);
    if (rc != 0) return rc;
    *val = (i32)(i16)le16(b);
    return 0;
}

/* read the min/max/res trio for one control into cam->rng[row] */
static void uvc_ctrl_range(camera_dev_t *c, int row, u8 unit, u8 cs)
{
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    u8 ifno = unit ? c->pu_unit : c->ct_unit;
    if (!ifno) return;
    u8 b[4];
    driver_usb_setup_t s;
    s.bmRequestType = 0xA1;
    s.wValue = (u16)(cs << 8);
    s.wIndex = (u16)((ifno << 8) | uc->vc_if);
    s.wLength = 2;
    s.bRequest = VC_REQ_GET_MIN;
    if (driver_usb_control_transfer(uc->dev, &s, b, 2) == 0)
        c->rng[row][0] = (i32)(i16)le16(b);
    s.bRequest = VC_REQ_GET_MAX;
    if (driver_usb_control_transfer(uc->dev, &s, b, 2) == 0)
        c->rng[row][1] = (i32)(i16)le16(b);
    s.bRequest = VC_REQ_GET_RES;
    if (driver_usb_control_transfer(uc->dev, &s, b, 2) == 0)
        c->rng[row][2] = (i32)(i16)le16(b);
}

/* ------------------------------------------------------------------ */
/* USB class-driver probe                                              */
/* ------------------------------------------------------------------ */

static const camera_ops_t uvc_ops = {
    .probe       = uvc_probe_ctrl,
    .commit      = uvc_commit_ctrl,
    .stream_start = uvc_stream_start,
    .stream_stop  = uvc_stream_stop,
    .capture     = uvc_capture_frame,
    .ctrl_set    = uvc_ctrl_set,
    .ctrl_get    = uvc_ctrl_get,
};

/* bind an enumerated USB device that passed the UVC match */
static int uvc_bind(driver_usb_dev_t *dev)
{
    if (g_uvc_n >= 4) return -1;

    uvc_cam_t *uc = (uvc_cam_t *)kmalloc(sizeof(*uc));
    if (!uc) return -1;
    memset(uc, 0, sizeof(*uc));
    uc->dev = dev;
    uc->alt0 = 0;
    uc->alt_stream = 1;

    camera_dev_t *cam = &uc->cam;
    cam->ops = &uvc_ops;
    cam->priv = uc;

    if (uvc_parse_device(uc) != 0) {
        kfree(uc);
        return -1;
    }

    /* caps */
    cam->caps = CAM_CAP_STREAMING;
    for (u16 i = 0; i < cam->n_formats; i++) {
        switch (cam->formats[i].fmt) {
        case CAM_FMT_MJPEG: cam->caps |= CAM_CAP_MJPEG; break;
        case CAM_FMT_YUY2:  cam->caps |= CAM_CAP_YUY2; break;
        case CAM_FMT_H264:  cam->caps |= CAM_CAP_H264; break;
        }
    }
    if (uc->stream_ep_attr == USB_EP_ATTR_ISO) cam->caps |= CAM_CAP_ISO;
    if (uc->stream_ep_attr == USB_EP_ATTR_BULK) cam->caps |= CAM_CAP_BULK;
    if (cam->ct_unit) cam->caps |= CAM_CAP_TERMINAL_CT;
    if (cam->pu_unit) cam->caps |= CAM_CAP_TERMINAL_PU;

    /* frame ring: two slots sized to the largest declared frame */
    u32 max_frame = 0;
    for (u16 i = 0; i < cam->n_formats; i++)
        for (u16 fr = 0; fr < cam->formats[i].n_frames; fr++) {
            u32 sz = cam->formats[i].frames[fr].w *
                     cam->formats[i].frames[fr].h;
            if (sz > max_frame) max_frame = sz;
        }
    u32 ring_sz = max_frame ? max_frame : (320 * 240);
    ring_sz = (ring_sz * 2) / 2 + 8192;   /* + payload header slack  */
    if (ring_sz > 1024 * 1024) ring_sz = 1024 * 1024;
    for (int i = 0; i < 2; i++) {
        uc->ring[i] = (u8 *)kmalloc(ring_sz);
        if (!uc->ring[i]) {
            for (int k = 0; k < i; k++) kfree(uc->ring[k]);
            kfree(uc);
            return -1;
        }
        uc->ring_len[i] = ring_sz;
    }
    uc->ring_fill = 0;
    uc->ring_head = 0;

    if (camera_register(cam) != 0) {
        for (int i = 0; i < 2; i++) kfree(uc->ring[i]);
        kfree(uc);
        return -1;
    }

    /* pull control ranges for the six mapped controls */
    if (cam->pu_unit) {
        uvc_ctrl_range(cam, 0, 1, CAM_CS_BRIGHTNESS);
        uvc_ctrl_range(cam, 1, 1, CAM_CS_CONTRAST);
        uvc_ctrl_range(cam, 2, 1, CAM_CS_SATURATION);
    }
    if (cam->ct_unit) {
        uvc_ctrl_range(cam, 3, 0, CAM_CS_EXPOSURE_TIME_ABS);
        uvc_ctrl_range(cam, 4, 0, CAM_CS_FOCUS_ABS);
        uvc_ctrl_range(cam, 5, 0, CAM_CS_ZOOM_ABS);
    }

    dev->class_priv = uc;
    g_uvc_cams[g_uvc_n++] = uc;
    return 0;
}

static int uvc_usb_probe(driver_usb_dev_t *dev)
{
    /* match: device or interface class 0x0E (video) */
    if (dev->class == 0x0E || dev->subclass == 0x01 || dev->subclass == 0x02)
        return uvc_bind(dev);
    /* also scan the raw config for a VC/VS interface pair (some cameras
     * report per-interface classes the core surfaces differently) */
    const u8 *d = dev->cfg_raw;
    u32 p = 0;
    int has_vc = 0, has_vs = 0;
    while (p + 2 <= dev->cfg_len) {
        u8 bLength = d[p];
        if (bLength < 2 || p + bLength > dev->cfg_len) break;
        if (d[p + 1] == USB_DT_INTERFACE && bLength >= 9) {
            if (d[p + 5] == 0x0E && d[p + 6] == 0x01) has_vc = 1;
            if (d[p + 5] == 0x0E && d[p + 6] == 0x02) has_vs = 1;
        }
        p += bLength;
    }
    if (has_vc && has_vs) return uvc_bind(dev);
    return -1;     /* not ours */
}

static void uvc_usb_disconnect(driver_usb_dev_t *dev)
{
    for (int i = 0; i < g_uvc_n; i++) {
        uvc_cam_t *uc = g_uvc_cams[i];
        if (uc->dev == dev) {
            camera_unregister(&uc->cam);
            dev->class_priv = 0;
            for (int k = 0; k < 2; k++)
                if (uc->ring[k]) { kfree(uc->ring[k]); uc->ring[k] = 0; }
            for (int k = i; k < g_uvc_n - 1; k++)
                g_uvc_cams[k] = g_uvc_cams[k + 1];
            g_uvc_n--;
            kfree(uc);
            return;
        }
    }
}

/* 3.0.0-WP03 G3: file-local USB class-driver identity (was the
 * explicit driver_camera_uvc_register() call from kmain).  Note the
 * collection order change: the class driver is now registered BEFORE
 * host enumeration (driver_usb_probe_all collects first), so UVC
 * cameras present at power-on bind immediately instead of waiting
 * for a later hot-plug event - strictly more complete, zero effect
 * without UVC hardware. */
static const driver_usb_class_ops_t g_usbcls_uvc = {
    "uvc", 0x0E,
    uvc_usb_probe, uvc_usb_disconnect,
};
OC_DRIVER_DECL("usb", "uvc", &g_usbcls_uvc,
               "USB video class (UVC camera)");

int driver_camera_uvc_test_bind(driver_usb_dev_t *dev)
{
    return uvc_usb_probe(dev);
}

void driver_camera_uvc_test_disconnect(driver_usb_dev_t *dev)
{
    uvc_usb_disconnect(dev);
}

int driver_camera_uvc_descriptors(camera_dev_t *c, u8 *out, u16 max)
{
    if (!c || !out) return -1;
    uvc_cam_t *uc = (uvc_cam_t *)c->priv;
    if (!uc || !uc->dev) return -1;
    u16 n = uc->dev->cfg_len;
    if (n > max) n = max;
    memcpy(out, uc->dev->cfg_raw, n);
    return n;
}
