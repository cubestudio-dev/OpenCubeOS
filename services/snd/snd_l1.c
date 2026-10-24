/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/snd L1 surface (SYS_L1SVC cat "snd"):
 * one op per migrated v1 command (services/snd/snd_cmds.c), executed
 * captured. */
#include "snd_svc.h"
#include "drv_internal.h"
#include "core_registry.h"
#include "l1svc_defs.h"
#include "snd_cmds.h"
#include "shell.h"

typedef int (*snd_cmd_fn_t)(const char *);

static int snd_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    snd_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_SND_SOUND:        fn = snd_cmd_sound; break;
        case L1SVC_SND_HDA:          fn = snd_cmd_hda; break;
        case L1SVC_SND_AC97:         fn = snd_cmd_ac97; break;
        case L1SVC_SND_SB16:         fn = snd_cmd_sb16; break;
        case L1SVC_SND_ES1370:       fn = snd_cmd_es1370; break;
        case L1SVC_SND_VIRTIOSND:    fn = snd_cmd_virtiosnd; break;
        case L1SVC_SND_USBAUDIO:     fn = snd_cmd_usbaudio; break;
        case L1SVC_SND_PLAY:         fn = snd_cmd_play; break;
        case L1SVC_SND_VOLUME:       fn = snd_cmd_volume; break;
        case L1SVC_SND_HDA_TEST:     fn = snd_cmd_hda_test; break;
        case L1SVC_SND_AC97_TEST:    fn = snd_cmd_ac97_test; break;
        case L1SVC_SND_SB16_TEST:    fn = snd_cmd_sb16_test; break;
        case L1SVC_SND_ES1370_TEST:  fn = snd_cmd_es1370_test; break;
        case L1SVC_SND_VIRTIO_TEST:  fn = snd_cmd_virtio_snd_test; break;
        case L1SVC_SND_USB_TEST:     fn = snd_cmd_usb_audio_test; break;
        case L1SVC_SND_RW_TEST:      fn = snd_cmd_audio_rw_test; break;
        case L1SVC_SND_RATE_TEST:    fn = snd_cmd_sample_rate_test; break;
        default: return -1;
    }
    return shell_run_fn_captured(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0: the snd service identity now carries its ring-3 surface
 * (init/late unchanged from the group D declaration). */
OC_SERVICE_DECL_L1("snd", "audio service (card table + dispatch + probe collection + ring-3 surface)",
                   snd_svc_init, snd_svc_probe_all,
                   snd_l1_dispatch);
