/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/snd command surface (migrated
 * verbatim from recovery/shell_cmds_snd_test.c). */
#ifndef OC_SND_CMDS_H
#define OC_SND_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

int snd_cmd_sound(const char *args);
int snd_cmd_hda(const char *args);
int snd_cmd_ac97(const char *args);
int snd_cmd_sb16(const char *args);
int snd_cmd_es1370(const char *args);
int snd_cmd_virtiosnd(const char *args);
int snd_cmd_usbaudio(const char *args);
int snd_cmd_play(const char *args);
int snd_cmd_volume(const char *args);
int snd_cmd_hda_test(const char *args);
int snd_cmd_ac97_test(const char *args);
int snd_cmd_sb16_test(const char *args);
int snd_cmd_es1370_test(const char *args);
int snd_cmd_virtio_snd_test(const char *args);
int snd_cmd_usb_audio_test(const char *args);
int snd_cmd_audio_rw_test(const char *args);
int snd_cmd_sample_rate_test(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_SND_CMDS_H */
