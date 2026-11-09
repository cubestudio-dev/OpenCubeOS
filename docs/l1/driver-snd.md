<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: sound drivers

## The surface

`drivers/snd/driver_snd.h` (28 declarations; the numbered 26):

```
driver_snd_register(name, ops, ctx)
driver_snd_play(dev, samples, count)
driver_snd_stop(dev)
driver_snd_set_rate(dev, hz)
driver_snd_set_volume(dev, pct)
driver_snd_get_caps(dev, out)
```

Plus `driver_snd_probe_all`, `driver_snd_make_tone`, the per-hw init
entries (hda/ac97/sb16/es1370/virtio) and the lookup/listing
helpers.

## Bring-up notes per family

- **HDA**: codecs + streams, the widget graph.
- **AC97**: the mixer registers, the AC-link timing.
- **SB16**: DSP commands + DMA16.
- **ES1370**: the mixed playback channels.
- **virtio-snd**: the virtqueue protocol (and `-device virtio-sound`
  in QEMU).

`play` (a tone), `sound` (status), the `*_test` commands, and
`virtio_snd_test`/`sample_rate_test` are your gates. ISO endpoints
zero-pad their tails (no OOB reads - the audio audit fixed that).
