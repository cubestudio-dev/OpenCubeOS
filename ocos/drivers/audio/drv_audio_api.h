/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/snd: driver-internal interface.
 *
 * Kernel-internal ONLY: sound-card drivers (the sound driver files and the
 * USB audio class driver) include this header to add a probed card to
 * the audio service table.  This is NOT part of the L1 surface: the
 * v2.0.0 interface-surface rule removed the register-class entry
 * points from the public headers (snd_svc.h); registration is now
 * file-local identity declarations (OC_DRIVER_DECL) + probe-time
 * snd_dev_add() from inside the driver itself.
 */
#ifndef OC_SND_DRV_INTERNAL_H
#define OC_SND_DRV_INTERNAL_H

#include "types.h"

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


#ifdef __cplusplus
extern "C" {
#endif

/* Probe descriptor each snd driver declares in its own file:
 *   static const snd_drv_ops_t g_probe = { my_probe };
 *   OC_DRIVER_DECL("snd", "ac97", &g_probe, "AC'97 audio");
 * my_probe() finds the hardware and adds the card via snd_dev_add(). */
typedef struct {
    int (*probe)(void);   /* 0 = a card was found and added */
} snd_drv_ops_t;

/* Add a probed card to the audio service table (was the L1-surface
 * driver_snd_register(); internal since v2.0.0).  dev->name and
 * dev->type must be filled in; ops->play and ops->stop are mandatory.
 * Returns the device index (>= 0) or -1 when the table is full or the
 * arguments are invalid.  On success dev->index is filled in. */
int snd_dev_add(driver_snd_device_t *dev, const driver_snd_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* OC_SND_DRV_INTERNAL_H */
