/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/snd.c
 * Purpose: Sound card driver framework - registry + dispatch.
 *
 * The framework keeps a static table of registered sound cards and
 * routes framework calls (snd_play / snd_stop / snd_set_rate /
 * snd_set_volume / snd_get_caps) to the owning driver.  Volume and rate
 * bookkeeping lives here so drivers only touch hardware.
 *
 * snd_probe_all() runs once from kmain: it probes the six WP-10c
 * families in priority order.  Families without hardware in the current
 * environment simply fail their probe (that is normal: e.g. QEMU has
 * no virtio-snd-pci device model and SB16 is optional).
 */
#include "snd.h"
#include "console.h"
#include "string.h"
#include "sched.h"
#include "timer.h"

static snd_device_t g_snd_devices[SND_MAX_DEVICES];
static int g_snd_count = 0;

void snd_init(void) {
    /* Nothing: registration is driven by the per-family probes. */
}

const char *snd_type_name(snd_type_t type) {
    switch (type) {
    case SND_TYPE_HDA:       return "hda";
    case SND_TYPE_AC97:      return "ac97";
    case SND_TYPE_SB16:      return "sb16";
    case SND_TYPE_ES1370:    return "es1370";
    case SND_TYPE_VIRTIO:    return "virtio-snd";
    case SND_TYPE_USB_AUDIO: return "usb-audio";
    default:                 return "unknown";
    }
}

int snd_register(snd_device_t *dev, const snd_ops_t *ops) {
    if (!dev || !ops || !ops->play || !ops->stop) return -1;
    if (g_snd_count >= SND_MAX_DEVICES) return -1;

    snd_device_t *slot = &g_snd_devices[g_snd_count];
    *slot = *dev;
    slot->present = 1;
    slot->ops = ops;
    slot->index = g_snd_count;
    *dev = *slot;
    g_snd_count++;
    return slot->index;
}

int snd_num_devices(void) {
    return g_snd_count;
}

snd_device_t *snd_get_device(int idx) {
    if (idx < 0 || idx >= g_snd_count) return NULL;
    return &g_snd_devices[idx];
}

int snd_find_by_type(snd_type_t type) {
    for (int i = 0; i < g_snd_count; i++) {
        if (g_snd_devices[i].present && g_snd_devices[i].type == type)
            return i;
    }
    return -1;
}

int snd_find_first_present(void) {
    for (int i = 0; i < g_snd_count; i++) {
        if (g_snd_devices[i].present) return i;
    }
    return -1;
}

int snd_play(snd_device_t *dev, const void *buf, int len) {
    if (!dev || !dev->present || !dev->ops->play) return -1;
    if (!buf || len <= 0) return -1;

    int rc = dev->ops->play(dev, buf, len);
    if (rc > 0) dev->played_bytes += (u64)rc;
    return rc;
}

int snd_stop(snd_device_t *dev) {
    if (!dev || !dev->present || !dev->ops->stop) return -1;
    return dev->ops->stop(dev);
}

int snd_set_rate(snd_device_t *dev, u32 rate) {
    if (!dev || !dev->present) return -1;
    if (rate == 0) return -1;
    if (dev->ops->set_rate) {
        int rc = dev->ops->set_rate(dev, rate);
        if (rc == 0) dev->rate = rate;
        return rc;
    }
    /* Driver cannot reprogram: keep the framework bookkeeping honest. */
    return -1;
}

int snd_set_volume(snd_device_t *dev, u32 vol) {
    if (!dev || !dev->present) return -1;
    if (vol > 100) vol = 100;
    if (dev->ops->set_volume) {
        int rc = dev->ops->set_volume(dev, vol);
        if (rc == 0) dev->volume = vol;
        return rc;
    }
    dev->volume = vol;   /* framework-level volume (silently scaled) */
    return 0;
}

int snd_get_caps(snd_device_t *dev, snd_caps_t *caps) {
    if (!dev || !dev->present || !caps) return -1;
    caps->rates = 0;
    caps->min_channels = 1;
    caps->max_channels = 2;
    caps->bits8 = 0;
    caps->bits16 = 1;
    caps->bits32 = 0;
    caps->reserved = 0;
    caps->cur_rate = dev->rate;
    caps->cur_volume = dev->volume;
    if (dev->ops->get_caps) return dev->ops->get_caps(dev, caps);
    return 0;
}

void snd_list_devices(void) {
    char line[128];
    char n[24];

    oc_console_puts("sound devices:\n");
    if (g_snd_count == 0) {
        oc_console_puts("  (none)\n");
        return;
    }
    for (int i = 0; i < g_snd_count; i++) {
        snd_device_t *d = &g_snd_devices[i];
        oc_strcpy(line, "  ");
        oc_u64_to_str((u64)i, n); oc_strcat(line, n);
        oc_strcat(line, ": ");
        oc_strcat(line, d->name);
        oc_strcat(line, " (");
        oc_strcat(line, snd_type_name(d->type));
        oc_strcat(line, ") rate=");
        oc_u64_to_str(d->rate, n); oc_strcat(line, n);
        oc_strcat(line, " ch=");
        oc_u64_to_str(d->channels, n); oc_strcat(line, n);
        oc_strcat(line, " vol=");
        oc_u64_to_str(d->volume, n); oc_strcat(line, n);
        oc_strcat(line, " played=");
        oc_u64_to_str(d->played_bytes, n); oc_strcat(line, n);
        oc_strcat(line, "B\n");
        oc_console_puts(line);
    }
}

int snd_probe_all(void) {
    int any = 0;

    /* Priority order: mainstream first (HDA), then legacy PCI cards,
     * then the hot-pluggable / virtual buses. */
    if (hda_init(NULL) == 0)        any++;
    if (ac97_init(NULL) == 0)       any++;
    if (es1370_init(NULL) == 0)     any++;
    if (usb_audio_init(NULL) == 0)  any++;
    if (sb16_init(NULL) == 0)       any++;
    if (virtio_snd_init(NULL) == 0) any++;

    return any ? 0 : -1;
}

/* ---- PCM test-tone generator (sine, 16-bit LE stereo) ----
 * Fixed-point sine synthesis: phase accumulator + 64-entry quarter-wave
 * table, good enough for test tones and free of libc dependencies. */
static const i16 sine_tab[64] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170, 25329,
    27245, 28898, 30273, 31356, 32137, 32609, 32767, 32609, 32137,
    31356, 30273, 28898, 27245, 25329, 23170, 20787, 18204, 15446,
    12539, 9512, 6393, 3212, 0, -3212, -6393, -9512, -12539, -15446,
    -18204, -20787, -23170, -25329, -27245, -28898, -30273, -31356,
    -32137, -32609, -32767, -32609, -32137, -31356, -30273, -28898,
    -27245, -25329, -23170, -20787, -18204, -15446, -12539, -9512,
    -6393, -3212,
};

int snd_make_tone(void *buf, int cap_bytes, u32 rate, u32 freq,
                  int channels, int ms) {
    if (!buf || cap_bytes <= 0 || rate == 0 || channels < 1) return 0;
    u8 *out = (u8 *)buf;
    int frames = ((u64)rate * ms) / 1000;
    if (frames <= 0) frames = 1;
    int need = frames * channels * 2;
    if (need > cap_bytes) {
        frames = cap_bytes / (channels * 2);
        need = frames * channels * 2;
    }
    u64 step = ((u64)freq << 20) / rate;   /* phase step per frame, 1<<20 = 2*pi */
    u64 phase = 0;
    for (int f = 0; f < frames; f++) {
        u32 idx = (u32)((phase >> 20) & 63);
        i16 s = sine_tab[idx];
        for (int c = 0; c < channels; c++) {
            *out++ = (u8)(s & 0xFF);
            *out++ = (u8)((s >> 8) & 0xFF);
        }
        phase += step;
    }
    return need;
}
