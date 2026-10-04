/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/driver_usb_audio.c
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
 * Playback flow (driver_snd_play):
 *   while data remains:
 *     - wait for the USB frame number to change (1 ms frame)
 *     - copy the next <wMaxPacketSize> bytes into the ISO DMA buffer
 *     - re-arm the frame's ISO TD (driver_usb_iso_out_submit)
 *   The controller DMAs the packet to the endpoint every frame - the
 *   data path to the audio backend is real isochronous traffic.
 */
#include "driver_usb_audio.h"
#include "driver_usb.h"
#include "driver_pci.h"
#include "screen_console.h"
#include "lib_string.h"
#include "driver_snd.h"
#include "core_sched.h"
#include "core_timer.h"

/* UAC descriptor types / subtypes */
#define USB_DT_CS_INTERFACE   0x24
#define USB_DT_CS_ENDPOINT    0x25
#define USB_SUBCLASS_AUDIOSTREAMING 0x02

/* QEMU usb-audio packet geometry */
#define UAC_PACKET_BYTES      192    /* 96 stereo frames * 4 bytes */

typedef struct driver_usb_audio_dev {
    driver_usb_dev_t *dev;
    u16 packet_bytes;
    u32 rate;
    u8  channels;
    u8  audio_if_num;      /* streaming interface number */
    u8  audio_if_alt;      /* streaming alternate setting */
    u8  driver_usb_iso_out_ep;        /* ISO OUT endpoint number */
    u16 driver_usb_iso_out_maxpack;
    int has_audio_stream;
    int stream_active;
    u64 packets_sent;
    int up;
} driver_usb_audio_dev_t;

static driver_usb_audio_dev_t g_usbaudio;

/* forward decls */
static int driver_usb_audio_ops_play(driver_snd_device_t *sdev, const void *buf, int len);
static int driver_usb_audio_ops_stop(driver_snd_device_t *sdev);
static int driver_usb_audio_ops_set_rate(driver_snd_device_t *sdev, u32 rate);
static int driver_usb_audio_ops_set_volume(driver_snd_device_t *sdev, u32 vol);
static int driver_usb_audio_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps);
static const driver_snd_ops_t driver_usb_audio_snd_ops = {
    .play       = driver_usb_audio_ops_play,
    .stop       = driver_usb_audio_ops_stop,
    .set_rate   = driver_usb_audio_ops_set_rate,
    .set_volume = driver_usb_audio_ops_set_volume,
    .get_caps   = driver_usb_audio_ops_get_caps,
};

static void driver_usb_audio_log(const char *s) { screen_console_puts(s); }

/* Walk the raw configuration descriptor: find the audio-streaming
 * interface and its isochronous OUT endpoint. */
static int driver_usb_audio_parse_config(driver_usb_dev_t *dev, driver_usb_audio_dev_t *ad) {
    const u8 *p = dev->cfg_raw;
    int len = dev->cfg_len;
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
                ad->audio_if_num = p[2];
                ad->audio_if_alt = alt;
            }
        } else if (dtype == 0x05 && dlen >= 7 && in_audio_iface) {
            /* endpoint descriptor */
            u8 ep_addr = p[2];
            u8 attrs = p[3];
            u16 maxpack = (u16)(p[4] | (p[5] << 8));
            if ((ep_addr & 0x80) == 0 &&            /* OUT */
                (attrs & 3) == 1) {                 /* isochronous */
                ad->driver_usb_iso_out_ep = (u8)(ep_addr & 0x0f);
                ad->driver_usb_iso_out_maxpack = maxpack;
                ad->has_audio_stream = 1;
            }
        }
        p += dlen;
        len -= dlen;
    }
    ad->packet_bytes = ad->driver_usb_iso_out_maxpack;
    if (ad->packet_bytes == 0 || ad->packet_bytes > UAC_PACKET_BYTES)
        ad->packet_bytes = UAC_PACKET_BYTES;
    return ad->has_audio_stream ? 0 : -1;
}

/* ---- playback ---- */

/* current USB frame number (0..1023, increments every 1 ms) */
static u16 driver_usb_audio_frnum(void) {
    /* FRNUM register sits at io+6; usb.c owns the io base, so read it
     * through the exposed state via a tiny helper in usb.c */
    extern u16 driver_usb_uhci_frnum(void);
    return driver_usb_uhci_frnum();
}

static int driver_usb_audio_ops_play(driver_snd_device_t *sdev, const void *buf, int len) {
    driver_usb_audio_dev_t *ad = (driver_usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up || !ad->dev) return -1;
    const u8 *src = (const u8 *)buf;
    int sent = 0;
    u16 pkt = ad->packet_bytes;

    /* first play: activate the streaming alternate setting */
    if (!ad->stream_active) {
        if (driver_usb_set_interface(ad->dev, ad->audio_if_num,
                              ad->audio_if_alt) != 0) {
            driver_usb_audio_log("usbaudio: SET_INTERFACE failed\n");
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
        u16 f0 = driver_usb_audio_frnum();
        u64 deadline = core_timer_now_ms() + 100;
        while (driver_usb_audio_frnum() == f0) {
            if (core_timer_now_ms() > deadline) break;
            core_sched_yield();
        }

        u8 zero[4] = { 0, 0, 0, 0 };
        (void)zero;
        if (driver_usb_isochronous_transfer(ad->dev, ad->driver_usb_iso_out_ep,
                                     (void *)(src + sent), chunk) != 0)
            return sent ? sent : -1;
        ad->packets_sent++;
        sent += chunk;
    }
    return sent;
}

static int driver_usb_audio_ops_stop(driver_snd_device_t *sdev) {
    driver_usb_audio_dev_t *ad = (driver_usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up) return -1;
    ad->stream_active = 0;
    /* deactivate the streaming alternate setting (alt 0 = no endpoint) */
    driver_usb_set_interface(ad->dev, ad->audio_if_num, 0);
    return 0;
}

static int driver_usb_audio_ops_set_rate(driver_snd_device_t *sdev, u32 rate) {
    driver_usb_audio_dev_t *ad = (driver_usb_audio_dev_t *)sdev->priv;
    if (!ad || !ad->up) return -1;
    /* UAC1 devices advertise fixed rates per alternate setting; the
     * QEMU device runs 48 kHz.  Other rates are accepted as the
     * logical request and honoured when the device supports them. */
    if (rate == 0) return -1;
    ad->rate = rate;
    return 0;
}

static int driver_usb_audio_ops_set_volume(driver_snd_device_t *sdev, u32 vol) {
    (void)sdev; (void)vol;
    return 0;   /* UAC1 volume unit control not wired through here */
}

static int driver_usb_audio_ops_get_caps(driver_snd_device_t *sdev, driver_snd_caps_t *caps) {
    driver_usb_audio_dev_t *ad = (driver_usb_audio_dev_t *)sdev->priv;
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

int driver_usb_audio_init(void *driver_usb_dev) {
    driver_usb_audio_dev_t *ad = &g_usbaudio;
    if (ad->up) return 0;

    /* bring up the USB stack when not already done */
    if (driver_usb_init() != 0) return -1;
    if (driver_usb_enumerate() <= 0) {
        /* no (new) device on the bus */
        if (!driver_usb_dev && driver_usb_num_devices() == 0) return -1;
    }

    driver_usb_dev_t *dev = (driver_usb_dev_t *)driver_usb_dev;
    if (!dev) {
        for (int i = 0; i < USB_MAX_DEVICES; i++) {
            driver_usb_dev_t *d = driver_usb_get_device(i);
            if (d && (d->class == 0 || d->class == 0xEF || d->vid == 0x46f4)) {
                dev = d;   /* composite / QEMU audio candidate */
                break;
            }
        }
    }
    if (!dev) return -1;

    if (driver_usb_audio_parse_config(dev, ad) != 0) return -1;

    /* NOTE: the streaming alternate setting is activated lazily on the
     * first play (see driver_usb_audio_ops_play) - keeping alt 0 while idle
     * leaves the USB frame bandwidth and the audio voice untouched. */

    ad->dev = dev;
    ad->rate = 48000;
    ad->channels = 2;
    ad->up = 1;

    char line[128];
    char n[8];
    strcpy(line, "usb-audio: vid=0x");
    u64_to_hex(dev->vid, n, 4); strcat(line, n);
    strcat(line, " pid=0x");
    u64_to_hex(dev->pid, n, 4); strcat(line, n);
    strcat(line, " ep=0x");
    u64_to_hex(ad->driver_usb_iso_out_ep, n, 2); strcat(line, n);
    strcat(line, " maxpack=");
    u64_to_str(ad->driver_usb_iso_out_maxpack, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");

    driver_snd_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "usb-audio");
    nd.type = SND_TYPE_USB_AUDIO;
    nd.rate = ad->rate; nd.channels = ad->channels; nd.volume = 100;
    nd.priv = ad;
    int idx = driver_snd_register(&nd, &driver_usb_audio_snd_ops);
    return idx >= 0 ? 0 : -1;
}

void driver_usb_audio_print_state(void) {
    driver_usb_audio_dev_t *ad = &g_usbaudio;
    char line[128];
    char n[24];

    screen_console_puts("USB Audio: ");
    if (!ad->up) {
        screen_console_puts("not present\n");
        return;
    }
    strcpy(line, "vid=0x");
    u64_to_hex(ad->dev->vid, n, 4); strcat(line, n);
    strcat(line, " pid=0x");
    u64_to_hex(ad->dev->pid, n, 4); strcat(line, n);
    strcat(line, " ep=0x");
    u64_to_hex(ad->driver_usb_iso_out_ep, n, 2); strcat(line, n);
    strcat(line, " packet=");
    u64_to_str(ad->packet_bytes, n); strcat(line, n);
    strcat(line, "B rate=");
    u64_to_str(ad->rate, n); strcat(line, n);
    strcat(line, " pkts=");
    u64_to_str(ad->packets_sent, n); strcat(line, n);
    screen_console_puts(line);
    screen_console_puts("\n");
}

/* ---- WP-10d class-driver registration ---- */

#include "driver_usb.h"

static int driver_usb_audio_class_probe(driver_usb_dev_t *dev) {
    return driver_usb_audio_init((void *)dev);
}

static void driver_usb_audio_class_disconnect(driver_usb_dev_t *dev) {
    driver_usb_audio_dev_t *ad = &g_usbaudio;
    if (ad->up && ad->dev == dev) {
        if (ad->stream_active)
            driver_usb_set_interface(ad->dev, ad->audio_if_num, 0);
        memset(ad, 0, sizeof(*ad));
        screen_console_puts("usb-audio: device removed\n");
    }
}

int driver_usb_audio_class_register(void) {
    return driver_usb_register_driver("usb-audio", USB_CLASS_AUDIO,
                               driver_usb_audio_class_probe,
                               driver_usb_audio_class_disconnect);
}
