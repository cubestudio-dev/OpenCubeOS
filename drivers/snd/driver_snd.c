/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/snd.c
 * Purpose: Sound card driver framework - registry + dispatch.
 *
 * The framework keeps a static table of registered sound cards and
 * routes framework calls (driver_snd_play / driver_snd_stop / driver_snd_set_rate /
 * driver_snd_set_volume / driver_snd_get_caps) to the owning driver.  Volume and rate
 * bookkeeping lives here so drivers only touch hardware.
 *
 * driver_snd_probe_all() runs once from kmain: it probes the six WP-10c
 * families in priority order.  Families without hardware in the current
 * environment simply fail their probe (that is normal: e.g. QEMU has
 * no virtio-snd-pci device model and SB16 is optional).
 */
#include "driver_snd.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_sched.h"
#include "core_timer.h"

static driver_snd_device_t g_snd_devices[SND_MAX_DEVICES];
static int g_snd_count = 0;

void driver_snd_init(void) {
    /* Nothing: registration is driven by the per-family probes. */
}

const char *driver_snd_type_name(driver_snd_type_t type) {
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

int driver_snd_register(driver_snd_device_t *dev, const driver_snd_ops_t *ops) {
    if (!dev || !ops || !ops->play || !ops->stop) return -1;
    if (g_snd_count >= SND_MAX_DEVICES) return -1;

    driver_snd_device_t *slot = &g_snd_devices[g_snd_count];
    *slot = *dev;
    slot->present = 1;
    slot->ops = ops;
    slot->index = g_snd_count;
    *dev = *slot;
    g_snd_count++;
    return slot->index;
}

int driver_snd_num_devices(void) {
    return g_snd_count;
}

driver_snd_device_t *driver_snd_get_device(int idx) {
    if (idx < 0 || idx >= g_snd_count) return NULL;
    return &g_snd_devices[idx];
}

int driver_snd_find_by_type(driver_snd_type_t type) {
    for (int i = 0; i < g_snd_count; i++) {
        if (g_snd_devices[i].present && g_snd_devices[i].type == type)
            return i;
    }
    return -1;
}

int driver_snd_find_first_present(void) {
    for (int i = 0; i < g_snd_count; i++) {
        if (g_snd_devices[i].present) return i;
    }
    return -1;
}

int driver_snd_play(driver_snd_device_t *dev, const void *buf, int len) {
    if (!dev || !dev->present || !dev->ops->play) return -1;
    if (!buf || len <= 0) return -1;

    int rc = dev->ops->play(dev, buf, len);
    if (rc > 0) dev->played_bytes += (u64)rc;
    return rc;
}

int driver_snd_stop(driver_snd_device_t *dev) {
    if (!dev || !dev->present || !dev->ops->stop) return -1;
    return dev->ops->stop(dev);
}

int driver_snd_set_rate(driver_snd_device_t *dev, u32 rate) {
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

int driver_snd_set_volume(driver_snd_device_t *dev, u32 vol) {
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

int driver_snd_get_caps(driver_snd_device_t *dev, driver_snd_caps_t *caps) {
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

void driver_snd_list_devices(void) {
    char line[128];
    char n[24];

    screen_console_puts("sound devices:\n");
    if (g_snd_count == 0) {
        screen_console_puts("  (none)\n");
        return;
    }
    for (int i = 0; i < g_snd_count; i++) {
        driver_snd_device_t *d = &g_snd_devices[i];
        strcpy(line, "  ");
        u64_to_str((u64)i, n); strcat(line, n);
        strcat(line, ": ");
        strcat(line, d->name);
        strcat(line, " (");
        strcat(line, driver_snd_type_name(d->type));
        strcat(line, ") rate=");
        u64_to_str(d->rate, n); strcat(line, n);
        strcat(line, " ch=");
        u64_to_str(d->channels, n); strcat(line, n);
        strcat(line, " vol=");
        u64_to_str(d->volume, n); strcat(line, n);
        strcat(line, " played=");
        u64_to_str(d->played_bytes, n); strcat(line, n);
        strcat(line, "B\n");
        screen_console_puts(line);
    }
}

int driver_snd_probe_all(void) {
    int any = 0;

    /* Priority order: mainstream first (HDA), then legacy PCI cards,
     * then the hot-pluggable / virtual buses. */
    if (driver_snd_hda_init(NULL) == 0)        any++;
    if (driver_snd_ac97_init(NULL) == 0)       any++;
    if (driver_snd_es1370_init(NULL) == 0)     any++;
    if (driver_usb_audio_init(NULL) == 0)  any++;
    if (driver_snd_sb16_init(NULL) == 0)       any++;
    if (driver_snd_virtio_init(NULL) == 0) any++;

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

int driver_snd_make_tone(void *buf, int cap_bytes, u32 rate, u32 freq,
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
