<!-- SPDX-License-Identifier: Apache-2.0 -->
# Sound bring-up manual

## Families

| Driver | QEMU | Command | Test |
|--------|------|---------|------|
| HDA | `-device intel-hda -device hda-output` | `hda` | `hda_test` |
| AC97 | `-device AC97` | `ac97` | `ac97_test` |
| SB16 | `-device sb16` (ISA) | `sb16` | `sb16_test` |
| ES1370 | `-device es1370` | `es1370` | `es1370_test` |
| virtio-snd | `-device virtio-sound` | `virtiosnd` | `virtio_snd_test` |

## Bring-up order

1. `sound` - the family status + registered devices.
2. The per-family command shows codec/mixer/DSP state.
3. `play` - a tone through the default device.
4. Gates: the `*_test` per family + `sample_rate_test` +
   `audio_rw_test`.

## The L1 surface

`driver_snd_register` + play/stop/set_rate/set_volume/get_caps; see
[l1/driver-snd.md](../l1/driver-snd.md).
