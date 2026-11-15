<!-- SPDX-License-Identifier: Apache-2.0 -->
# Sound drivers

How an audio family joins the sound service.

## The model (3.0.0)

- Drivers live in `ocos/drivers/audio/` - one file per family:
  HDA (`drv_audio_hda.c`), AC'97, SB16, ES1370, virtio-snd; USB
  audio hangs off the USB class drivers
  (`drv_usb_audio.c`).
- Identity: `OC_DRIVER_DECL("snd", "hda", &g_snd_probe_hda,
  "Intel HD Audio")` - same collector as every other class.
- The device table + mixer + playback engine live in the sound
  service (`ocos/services/audio/`): device registry
  (`driver_snd_device_t`, name/channels/rate/volume), the tone
  synthesizer (`driver_snd_make_tone` - the 440 Hz test tone
  behind `play`), and the L1SVC category 4 surface.

## The service surface (17 ops)

`sound` (device overview), per-driver status (`hda`, `ac97`,
`sb16`, `es1370`, `virtiosnd`, `usbaudio`), `play [device]
[rate]` (the test tone), `volume [device] [0-100]`, and the
selftest family (`hda_test` .. `sample_rate_test`, the RW
loopback, the sample-rate accuracy probe).

## Defensive notes carried in the code

- A card reporting `channels == 0` must not divide by zero in the
  frame-count line (BUG-0243: the service reports 0 frames and the
  play reports the device error honestly).
- The `sound-tool` wizard validates the volume/rate numbers
  digits-only before they reach the service - keep that layering
  when adding fields.

## What a new family looks like

1. `ocos/drivers/audio/drv_audio_<fam>.{c,h}` - probe, descriptor
   parse, PCM stream setup.
2. `OC_DRIVER_DECL("snd", "<fam>", &ops, "description")`.
3. A status op (append-only numbering) + the `sound-tool` classic
   table row + the per-driver submenu entry.

## The consumer contract

`sound-tool sound` -> `sys_l1svc(SND, SOUND, ...)`; wizards and
scripts read the English device lines; `play`'s usage
(`play [device] [rate 4000-96000]`) is the stable spelling.
