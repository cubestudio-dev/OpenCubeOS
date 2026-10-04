/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/snd.h
 * Purpose: Sound card (audio) driver framework.
 *
 * WP-10c introduces a device-driver layer for sound cards, in the same
 * shape as the WP-10a block layer (blk.h) and the WP-10b NIC layer
 * (nic.h): a static registry of devices plus per-driver operations.
 * Applications (and the `play` shell command) feed PCM samples through
 * this layer; the per-family drivers in hda.c / ac97.c / sb16.c /
 * es1370.c / virtio_snd.c / driver_usb_audio.c implement the hardware paths.
 *
 * Supported driver families (WP-10c):
 *
 *   hda       Intel HD Audio (ICH6/82801 + compatibles)  (8086:2668, 293E, ...)
 *   ac97      Intel 82801AA/AB AC'97                     (8086:2415, 2445, ...)
 *   sb16      Creative Sound Blaster 16 (ISA/PnP)
 *   es1370    Ensoniq AudioPCI ES1370/ES1371             (1274:5000, 1371)
 *   virtio    virtio-sound (virtio-snd-pci)              (1AF4:1059)
 *   driver_usb_audio USB Audio Class (UAC 1.0) over UHCI        (e.g. QEMU 46f4:0002)
 *
 * All operations return 0 (or a positive byte count) on success and a
 * negative value on error, mirroring the blk/nic layer conventions.
 *
 * PCM data format used across the framework:
 *
 *   little-endian signed 16-bit, interleaved stereo (2 channels) by
 *   default at the device's current sample rate (driver_snd_device_t.rate).
 *   `play` takes BYTES, not samples.
 *
 * L1 extension surface (this header is the documented interface):
 *
 *   int driver_snd_register(driver_snd_device_t *dev, const driver_snd_ops_t *ops);
 *       Register a sound card with the framework.  dev->name and
 *       dev->type must be filled in; ops->play and ops->stop are
 *       mandatory.  Returns the device index (>= 0) or -1 when the
 *       table is full / arguments are invalid.  On success dev->index
 *       is filled in.
 *
 *   int driver_snd_play(driver_snd_device_t *dev, const void *buf, int len);
 *       Feed `len` bytes of PCM (S16LE stereo at dev->rate) to the
 *       device and wait until the driver has accepted all of them into
 *       its DMA ring (blocking, cooperative: the driver polls the DMA
 *       position and yields to other tasks).  Returns the number of
 *       bytes played (>= 0) or -1 on error.
 *
 *   int driver_snd_stop(driver_snd_device_t *dev);
 *       Stop the current stream and drain the DMA ring.  0 on success.
 *
 *   int driver_snd_set_rate(driver_snd_device_t *dev, u32 rate);
 *       Configure the sample rate (e.g. 44100, 48000).  0 on success,
 *       -1 when the rate is not supported.
 *
 *   int driver_snd_set_volume(driver_snd_device_t *dev, u32 vol);
 *       Set the playback volume 0..100.  0 on success.
 *
 *   int driver_snd_get_caps(driver_snd_device_t *dev, driver_snd_caps_t *caps);
 *       Copy the capability block (supported rates bitmap, channel
 *       range, bit widths, current rate).  0 on success.
 *
 *   int xxx_init(driver_pci_dev_t *pdev);   -- per-driver init entry points
 *       pdev == NULL: scan the PCI bus for the family's vendor/device
 *       IDs (or, for SB16/USB, the ISA IO / USB bus) and initialise the
 *       first match.  pdev != NULL: initialise exactly that PCI
 *       function.  Returns 0 on success, -1 when no device is present
 *       or init fails.  Drivers that initialise successfully register
 *       themselves with driver_snd_register().
 */
#ifndef OC_SND_H
#define OC_SND_H

#include "types.h"
#include "driver_pci.h"

/* ---- Registry limits ---- */
#define SND_MAX_DEVICES  8
#define SND_NAME_LEN     16

/* ---- Driver families ---- */
typedef enum {
    SND_TYPE_HDA       = 1,
    SND_TYPE_AC97      = 2,
    SND_TYPE_SB16      = 3,
    SND_TYPE_ES1370    = 4,
    SND_TYPE_VIRTIO    = 5,
    SND_TYPE_USB_AUDIO = 6,
} driver_snd_type_t;

/* ---- Rate bitmap (Linux/AC'97 convention: one bit per standard rate) ---- */
#define SND_RATE_8000   (1u << 0)
#define SND_RATE_11025  (1u << 1)
#define SND_RATE_16000  (1u << 2)
#define SND_RATE_22050  (1u << 3)
#define SND_RATE_32000  (1u << 4)
#define SND_RATE_44100  (1u << 5)
#define SND_RATE_48000  (1u << 6)
#define SND_RATE_64000  (1u << 7)
#define SND_RATE_88200  (1u << 8)
#define SND_RATE_96000  (1u << 9)

/* ---- Capabilities ---- */
typedef struct driver_snd_caps {
    u32 rates;          /* SND_RATE_* bitmap */
    u8  min_channels;   /* usually 1 */
    u8  max_channels;   /* 2 = stereo */
    u8  bits8;          /* 1 = 8-bit PCM supported */
    u8  bits16;         /* 1 = 16-bit PCM supported */
    u8  bits32;         /* 1 = 32-bit PCM supported */
    u8  reserved;
    u32 cur_rate;       /* rate currently programmed */
    u32 cur_volume;     /* 0..100 currently set */
} driver_snd_caps_t;

/* Per-driver operations.  play/stop are mandatory; set_rate/set_volume/
 * get_caps may be NULL (the framework then keeps its bookkeeping and
 * reports -1 for unsupported operations). */
typedef struct driver_snd_device driver_snd_device_t;

typedef struct driver_snd_ops {
    int (*play)(driver_snd_device_t *dev, const void *buf, int len);
    int (*stop)(driver_snd_device_t *dev);
    int (*set_rate)(driver_snd_device_t *dev, u32 rate);
    int (*set_volume)(driver_snd_device_t *dev, u32 vol);
    int (*get_caps)(driver_snd_device_t *dev, driver_snd_caps_t *caps);
} driver_snd_ops_t;

/* One registered sound card.  name/type/pci ids describe the hardware;
 * ops/priv bind the driver implementation; rate/channels/volume mirror
 * the currently programmed state. */
struct driver_snd_device {
    char       name[SND_NAME_LEN];
    driver_snd_type_t type;
    u8         present;
    u8         bus, dev, func;
    u16        vendor_id, device_id;
    u32        rate;          /* current sample rate, Hz */
    u8         channels;      /* current channel count (1 or 2) */
    u32        volume;        /* 0..100 */
    const driver_snd_ops_t *ops;
    void       *priv;
    int        index;
    u64        played_bytes;  /* bytes accepted by the driver */
    u64        irqs;          /* device interrupts observed */
};

/* ---- Framework API (L1 extension surface) ---- */
void driver_snd_init(void);

/* Register a sound card (see file-header documentation). */
int  driver_snd_register(driver_snd_device_t *dev, const driver_snd_ops_t *ops);

/* Feed PCM data to a device (blocking). */
int  driver_snd_play(driver_snd_device_t *dev, const void *buf, int len);

/* Stop playback / drain the stream. */
int  driver_snd_stop(driver_snd_device_t *dev);

/* Configure the sample rate. */
int  driver_snd_set_rate(driver_snd_device_t *dev, u32 rate);

/* Set the volume (0..100). */
int  driver_snd_set_volume(driver_snd_device_t *dev, u32 vol);

/* Query capabilities. */
int  driver_snd_get_caps(driver_snd_device_t *dev, driver_snd_caps_t *caps);

/* ---- Registry helpers ---- */
int  driver_snd_num_devices(void);
driver_snd_device_t *driver_snd_get_device(int idx);
int  driver_snd_find_by_type(driver_snd_type_t type);        /* index / -1 */
int  driver_snd_find_first_present(void);             /* index / -1 */
void driver_snd_list_devices(void);
const char *driver_snd_type_name(driver_snd_type_t type);

/* Probe every WP-10c driver family in priority order (hda, ac97,
 * es1370, driver_usb_audio, sb16, virtio-snd).  Returns 0 when at least one
 * card initialised and registered, -1 when none did.  Called once from
 * kmain after the network init. */
int  driver_snd_probe_all(void);

/* ---- Per-driver init entry points (see file-header documentation) ---- */
int  driver_snd_hda_init(driver_pci_dev_t *pdev);
int  driver_snd_ac97_init(driver_pci_dev_t *pdev);
int  driver_snd_sb16_init(void *isa_dev);           /* ISA: no bus object, arg unused */
int  driver_snd_es1370_init(driver_pci_dev_t *pdev);
int  driver_snd_virtio_init(driver_pci_dev_t *pdev);
int  driver_usb_audio_init(void *driver_usb_dev);      /* driver_usb_device_t* from usb.h */

/* ---- Per-driver status printers for the shell commands ---- */
void driver_snd_hda_print_state(void);
void driver_snd_ac97_print_state(void);
void driver_snd_sb16_print_state(void);
void driver_snd_es1370_print_state(void);
void driver_snd_virtio_print_state(void);
void driver_usb_audio_print_state(void);

/* WP-10c shell surface: tests + status commands (kernel/driver_snd_test_cmds.c). */
void shell_cmds_snd_test_register(void);

/* Generate a PCM test tone (sine wave) into buf: rate/freq/channels and
 * milliseconds.  Returns the number of bytes written.  Used by `play`
 * and the *_test commands. */
int  driver_snd_make_tone(void *buf, int cap_bytes, u32 rate, u32 freq,
                   int channels, int ms);

#endif /* OC_SND_H */
