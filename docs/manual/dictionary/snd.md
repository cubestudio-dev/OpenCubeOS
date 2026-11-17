<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Audio (17)

### ac97

| Field | Value |
|---|---|
| Name | ac97 |
| Category | Audio |
| Function | AC97 codec status |
| Usage | `ac97` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
ac97
AC'97: not present
ush> 
```

### ac97_test

| Field | Value |
|---|---|
| Name | ac97_test |
| Category | Audio |
| Function | AC97 playback test |
| Usage | `ac97_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
ac97_test
[ac97_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### audio_rw_test

| Field | Value |
|---|---|
| Name | audio_rw_test |
| Category | Audio |
| Function | audio read/write test |
| Usage | `audio_rw_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
audio_rw_test
[audio_rw_test] no sound cards registered => SKIPPED
ush> 
```

### es1370

| Field | Value |
|---|---|
| Name | es1370 |
| Category | Audio |
| Function | ES1370 codec status |
| Usage | `es1370` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
es1370
ES1370: not present
ush> 
```

(Note: absent-hardware probe (no ES1370 in this boot))

### es1370_test

| Field | Value |
|---|---|
| Name | es1370_test |
| Category | Audio |
| Function | ES1370 playback test |
| Usage | `es1370_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
es1370
ES1370: not present
ush> 
```

### hda

| Field | Value |
|---|---|
| Name | hda |
| Category | Audio |
| Function | HD Audio codec status |
| Usage | `hda` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
grep
usage: grep <pattern>
ush> 
```

### hda_test

| Field | Value |
|---|---|
| Name | hda_test |
| Category | Audio |
| Function | HD Audio playback test |
| Usage | `hda_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
grub-install
usage: grub-install <device>  (write the GRUB BIOS boot loader)
ush> 
```

### play

| Field | Value |
|---|---|
| Name | play |
| Category | Audio |
| Function | play a WAV file |
| Usage | `play <file>` |
| Parameters | `file` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
play
play: no sound card registered
ush> 
```

### sample_rate_test

| Field | Value |
|---|---|
| Name | sample_rate_test |
| Category | Audio |
| Function | sample rate test |
| Usage | `sample_rate_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
sample_rate_test
[sample_rate_test] no sound cards registered => SKIPPED
ush> 
```

### sb16

| Field | Value |
|---|---|
| Name | sb16 |
| Category | Audio |
| Function | SoundBlaster16 status |
| Usage | `sb16` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
sb16
Sound Blaster 16: not present
ush> 
```

### sb16_test

| Field | Value |
|---|---|
| Name | sb16_test |
| Category | Audio |
| Function | SB16 playback test |
| Usage | `sb16_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
sb16_test
[sb16_test] device not present in this environment => SKIPPED (no ISA SB16 (QEMU: add -device sb16,audiodev=snd0))
ush> 
```

### sound

| Field | Value |
|---|---|
| Name | sound |
| Category | Audio |
| Function | audio subsystem status |
| Usage | `sound` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
sound
sound devices:
  (none)
ush> 
```

### usb_audio_test

| Field | Value |
|---|---|
| Name | usb_audio_test |
| Category | Audio |
| Function | USB audio test |
| Usage | `usb_audio_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
usb_audio_test
[usb_audio_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### usbaudio

| Field | Value |
|---|---|
| Name | usbaudio |
| Category | Audio |
| Function | USB audio status |
| Usage | `usbaudio` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
usbaudio
USB Audio: not present
USB: not present
ush> 
```

### virtio_snd_test

| Field | Value |
|---|---|
| Name | virtio_snd_test |
| Category | Audio |
| Function | virtio-snd test |
| Usage | `virtio_snd_test` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
virtio_snd_test
[virtio_snd_test] device not present in this environment => SKIPPED (QEMU 10 has no virtio-snd-pci model)
ush> 
```

### virtiosnd

| Field | Value |
|---|---|
| Name | virtiosnd |
| Category | Audio |
| Function | virtio-sound status |
| Usage | `virtiosnd` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
virtiosnd
virtio-snd: not present
ush> 
```

### volume

| Field | Value |
|---|---|
| Name | volume |
| Category | Audio |
| Function | mixer volume control |
| Usage | `volume [0-100]` |
| Source | `ocos/services/audio/src/svc_audio_l1.c` |

**Executed example**:

```
volume
volume: no sound card registered
ush> 
```
