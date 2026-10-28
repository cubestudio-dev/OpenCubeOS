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

#include "snd_svc.h"

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
