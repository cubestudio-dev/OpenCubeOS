/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/usb_audio.c
 * Purpose: USB Audio Class 1.0 playback driver on the WP-10c UHCI stack.
 *
 * USB audio is the mainstream hot-pluggable sound path (USB dongles,
 * headsets, capture interfaces).  The class driver covers UAC 1.0
 * full-speed devices: it parses the raw configuration descriptor from
 * usb.c, locates the audio-streaming interface (class 0x01 subclass
 * 0x02) and its isochronous OUT endpoint, activates the streaming
 * alternate setting and feeds the endpoint one isochronous packet per
 * USB frame (1 ms).
 *
 * QEMU models the same thing: `-device usb-audio,audiodev=...` is a
 * UAC 1.0 full-speed device (VID 0x46f4 PID 0x0002) with a stereo
 * 16-bit 48 kHz streaming interface whose ISO OUT endpoint 0x01
 * accepts 192-byte packets (96 frames x 4 bytes).
 *
 * Playback flow (snd_play):
 *   while data remains:
 *     - wait for the USB frame number to change (1 ms frame)
 *     - copy the next <wMaxPacketSize> bytes into the ISO DMA buffer
 *     - re-arm the frame's ISO TD (usb_iso_out_submit)
 *   The controller DMAs the packet to the endpoint every frame - the
 *   data path to the audio backend is real isochronous traffic.
 */
#include "usb_audio.h"
#include "usb.h"
#include "pci.h"
#include "console.h"
#include "string.h"
#include "snd.h"
#include "sched.h"
#include "timer.h"

/* UAC descriptor types / subtypes */
#define USB_DT_CS_INTERFACE   0x24
#define USB_DT_CS_ENDPOINT    0x25
#define USB_SUBCLASS_AUDIOSTREAMING 0x02

/* QEMU usb-audio packet geometry */
#define UAC_PACKET_BYTES      192    /* 96 stereo frames * 4 bytes */

typedef struct usb_audio_dev {
    usb_device_t *dev;
    u16 packet_bytes;
    u32 rate;
    u8  channels;
    int stream_active;
    u64 packets_sent;
    int up;
} usb_audio_dev_t;

static usb_audio_dev_t g_usbaudio;

/* forward decls */
static int usbaudio_ops_play(snd_device_t *sdev, const void *buf, int len);
static int usbaudio_ops_stop(snd_device_t *sdev);
static int usbaudio_ops_set_rate(snd_device_t *sdev, u32 rate);
static int usbaudio_ops_set_volume(snd_device_t *sdev, u32 vol);
static int usbaudio_ops_get_caps(snd_device_t *sdev, snd_caps_t *caps);
static const snd_ops_t usbaudio_snd_ops = {
    .play       = usbaudio_ops_play,
    .stop       = usbaudio_ops_stop,
    .set_rate   = usbaudio_ops_set_rate,
    .set_volume = usbaudio_ops_set_volume,
    .get_caps   = usbaudio_ops_get_caps,
};

static void usbaudio_log(const char *s) { oc_console_puts(s); }

/* Walk the raw configuration descriptor: find the audio-streaming
 * interface and its isochronous OUT endpoint. */
static int usbaudio_parse_config(usb_device_t *dev, usb_audio_dev_t *ad) {
    const u8 *p = dev->raw_cfg;
    int len = dev->raw_cfg_len;
    int in_audio_iface = 0;

    while (len >= 2) {
        u8 dlen = p[0];
        u8 dtype = p[1];
        if (dlen < 2 || dlen > len) break;

        if (dtype == 0x04 && dlen >= 9) {          /* interface descriptor */
            u8 cls = p[5];
            u8 sub = p[6];
            u8 alt = p[3];
            u8 n_ep = p[4];
            in_audio_iface = (cls == 0x01 && sub == USB_SUBCLASS_AUDIOSTREAMING);
            if (in_audio_iface && n_ep > 0) {
                dev->audio_if_num = p[2];
                dev->audio_if_alt = alt;
            }
        } else if (dtype == 0x05 && dlen >= 7 && in_audio_iface) {
            /* endpoint descriptor */
            u8 ep_addr = p[2];
            u8 attrs = p[3];
            u16 maxpack = (u16)(p[4] | (p[5] << 8));
            if ((ep_addr & 0x80) == 0 &&            /* OUT */
                (attrs & 3) == 1) {                 /* isochronous */
                dev->iso_out_ep = (u8)(ep_addr & 0x0f);
                dev->iso_out_maxpack = maxpack;
                dev->has_audio_stream = 1;
            }
        }
        p += dlen;
        len -= dlen;
    }
    ad->packet_bytes = dev->iso_out_maxpack;
    if (ad->packet_bytes == 0 || ad->packet_bytes > UAC_PACKET_BYTES)
        ad->packet_bytes = UAC_PACKET_BYTES;
    return dev->has_audio_stream ? 0 : -1;
}

/* ---- playback ---- */

/* current USB frame number (0..1023, increments every 1 ms) */
static u16 usbaudio_frnum(void) {
    /* FRNUM register sits at io+6; usb.c owns the io base, so read it
     * through the exposed state via a tiny helper in usb.c */
    extern u16 usb_uhci_frnum(void);
    return usb_uhci_frnum();
}

static int usbaudio_ops_play(snd_device_t *sdev, const void *buf, int len) {
    usb_audio_dev_t *ad = (usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up || !ad->dev) return -1;
    const u8 *src = (const u8 *)buf;
    int sent = 0;
    u16 pkt = ad->packet_bytes;

    /* first play: activate the streaming alternate setting */
    if (!ad->stream_active) {
        if (usb_set_interface(ad->dev, ad->dev->audio_if_num,
                              ad->dev->audio_if_alt) != 0) {
            usbaudio_log("usbaudio: SET_INTERFACE failed\n");
            return -1;
        }
    }
    ad->stream_active = 1;

    while (sent < len) {
        u16 chunk = pkt;
        if ((u32)chunk > (u32)(len - sent)) chunk = (u16)(len - sent);
        chunk &= ~3u;
        if (chunk < 4) chunk = 4;

        /* wait for the next frame boundary */
        u16 f0 = usbaudio_frnum();
        u64 deadline = oc_timer_now_ms() + 100;
        while (usbaudio_frnum() == f0) {
            if (oc_timer_now_ms() > deadline) break;
            sched_yield();
        }

        u8 zero[4] = { 0, 0, 0, 0 };
        (void)zero;
        if (usb_iso_out_submit(ad->dev, src + sent, chunk) != 0)
            return sent ? sent : -1;
        ad->packets_sent++;
        sent += chunk;
    }
    return sent;
}

static int usbaudio_ops_stop(snd_device_t *sdev) {
    usb_audio_dev_t *ad = (usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up) return -1;
    ad->stream_active = 0;
    /* deactivate the streaming alternate setting (alt 0 = no endpoint) */
    usb_set_interface(ad->dev, ad->dev->audio_if_num, 0);
    return 0;
}

static int usbaudio_ops_set_rate(snd_device_t *sdev, u32 rate) {
    usb_audio_dev_t *ad = (usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up) return -1;
    /* UAC1 devices advertise fixed rates per alternate setting; the
     * QEMU device runs 48 kHz.  Other rates are accepted as the
     * logical request and honoured when the device supports them. */
    if (rate == 0) return -1;
    ad->rate = rate;
    return 0;
}

static int usbaudio_ops_set_volume(snd_device_t *sdev, u32 vol) {
    (void)sdev; (void)vol;
    return 0;   /* UAC1 volume unit control not wired through here */
}

static int usbaudio_ops_get_caps(snd_device_t *sdev, snd_caps_t *caps) {
    usb_audio_dev_t *ad = (usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up) return -1;
    caps->rates = SND_RATE_48000;
    caps->min_channels = 1;
    caps->max_channels = 2;
    caps->bits8 = 0;
    caps->bits16 = 1;
    caps->bits32 = 0;
    caps->cur_rate = ad->rate;
    caps->cur_volume = sdev->volume;
    return 0;
}

/* ---- init ---- */

int usb_audio_init(void *usb_dev) {
    usb_audio_dev_t *ad = &g_usbaudio;
    if (ad->up) return 0;

    /* bring up the USB stack when not already done */
    if (usb_init() != 0) return -1;
    if (usb_enumerate() <= 0) {
        /* no (new) device on the bus */
        if (!usb_dev && usb_num_devices() == 0) return -1;
    }

    usb_device_t *dev = (usb_device_t *)usb_dev;
    if (!dev) {
        for (int i = 0; i < USB_MAX_DEVICES; i++) {
            usb_device_t *d = usb_get_device(i);
            if (d && (d->class == 0 || d->class == 0xEF || d->vid == 0x46f4)) {
                dev = d;   /* composite / QEMU audio candidate */
                break;
            }
        }
    }
    if (!dev) return -1;

    if (usbaudio_parse_config(dev, ad) != 0) return -1;

    /* NOTE: the streaming alternate setting is activated lazily on the
     * first play (see usbaudio_ops_play) - keeping alt 0 while idle
     * leaves the USB frame bandwidth and the audio voice untouched. */

    ad->dev = dev;
    ad->rate = 48000;
    ad->channels = 2;
    ad->up = 1;

    char line[128];
    char n[8];
    oc_strcpy(line, "usb-audio: vid=0x");
    oc_u64_to_hex(dev->vid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " pid=0x");
    oc_u64_to_hex(dev->pid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " ep=0x");
    oc_u64_to_hex(dev->iso_out_ep, n, 2); oc_strcat(line, n);
    oc_strcat(line, " maxpack=");
    oc_u64_to_str(dev->iso_out_maxpack, n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");

    snd_device_t nd;
    oc_memset(&nd, 0, sizeof(nd));
    oc_strcpy(nd.name, "usb-audio");
    nd.type = SND_TYPE_USB_AUDIO;
    nd.rate = ad->rate; nd.channels = ad->channels; nd.volume = 100;
    nd.priv = ad;
    int idx = snd_register(&nd, &usbaudio_snd_ops);
    return idx >= 0 ? 0 : -1;
}

void usb_audio_print_state(void) {
    usb_audio_dev_t *ad = &g_usbaudio;
    char line[128];
    char n[24];

    oc_console_puts("USB Audio: ");
    if (!ad->up) {
        oc_console_puts("not present\n");
        return;
    }
    oc_strcpy(line, "vid=0x");
    oc_u64_to_hex(ad->dev->vid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " pid=0x");
    oc_u64_to_hex(ad->dev->pid, n, 4); oc_strcat(line, n);
    oc_strcat(line, " ep=0x");
    oc_u64_to_hex(ad->dev->iso_out_ep, n, 2); oc_strcat(line, n);
    oc_strcat(line, " packet=");
    oc_u64_to_str(ad->packet_bytes, n); oc_strcat(line, n);
    oc_strcat(line, "B rate=");
    oc_u64_to_str(ad->rate, n); oc_strcat(line, n);
    oc_strcat(line, " pkts=");
    oc_u64_to_str(ad->packets_sent, n); oc_strcat(line, n);
    oc_console_puts(line);
    oc_console_puts("\n");
}
