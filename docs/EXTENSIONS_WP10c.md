<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# WP-10c: Sound Card Drivers

WP-10c adds mainstream sound card support to the L0 kernel: an audio
driver framework (kernel/snd.c) plus six driver families, the shell
surface to drive them, and a test suite.  Every driver does real DMA
and real interrupts; nothing is stubbed.

## Driver families

| Family    | File              | Hardware / enumeration                                    |
|-----------|-------------------|-----------------------------------------------------------|
| hda       | kernel/hda.c      | Intel HD Audio 8086:2668/293E/293F/3A3E + class 0x040300  |
| ac97      | kernel/ac97.c     | Intel AC'97 8086:2415/2445/2485/24C5/24D5                 |
| es1370    | kernel/es1370.c   | Ensoniq 1274:5000 / 1274:1371                             |
| sb16      | kernel/sb16.c     | ISA Sound Blaster 16, io 0x220, IRQ 5, 16-bit DMA ch 5    |
| virtio    | kernel/virtio_snd.c | virtio-snd-pci 1AF4:1059 (modern virtio-pci transport)  |
| usb-audio | kernel/usb_audio.c| USB Audio Class 1.0 over the WP-10c UHCI stack            |

## USB host stack (new)

`kernel/usb.c` implements the USB 1.1 host side needed by the audio
class driver: UHCI controller driver (8086:7020 piix3 / 8086:7112
piix4), blocking control transfers (SETUP + DATA + STATUS TD chains on
one queue head), device enumeration (descriptors, SET_ADDRESS,
SET_CONFIGURATION, SET_INTERFACE) and isochronous OUT scheduling (one
ISO TD per 1 ms frame, re-armed by the audio driver at frame cadence).

## Playback model

`snd_play()` feeds PCM (little-endian signed 16-bit, interleaved
stereo) into the driver's DMA ring and blocks until the driver has
accepted every byte; flow control polls the hardware DMA position
(HDA LPIB, AC'97 CIV+PICB, virtio used ring, USB frame number).  Each
driver raises real device interrupts and exposes the count through
`snd_device_t.irqs`.

## L1 extension surface (kernel/snd.h)

```c
void snd_init(void);
int  snd_register(snd_device_t *dev, const snd_ops_t *ops);
int  snd_play(snd_device_t *dev, const void *buf, int len);
int  snd_stop(snd_device_t *dev);
int  snd_set_rate(snd_device_t *dev, u32 rate);
int  snd_set_volume(snd_device_t *dev, u32 vol);
int  snd_get_caps(snd_device_t *dev, snd_caps_t *caps);
int  snd_num_devices(void);
snd_device_t *snd_get_device(int idx);
int  snd_find_by_type(snd_type_t type);
int  snd_find_first_present(void);
void snd_list_devices(void);
const char *snd_type_name(snd_type_t type);
int  snd_probe_all(void);
int  snd_make_tone(void *buf, int cap_bytes, u32 rate, u32 freq,
                   int channels, int ms);
```

Per-driver init entry points (each registers its device on success):

```c
int  hda_init(pci_dev_t *pdev);          /* NULL = scan the PCI bus   */
int  ac97_init(pci_dev_t *pdev);
int  sb16_init(void *isa_dev);           /* arg unused (ISA)          */
int  es1370_init(pci_dev_t *pdev);
int  virtio_snd_init(pci_dev_t *pdev);
int  usb_audio_init(void *usb_dev);      /* NULL = enumerate the bus  */
```

Per-family status printers (used by the shell commands and by the
`real_hw_test` WP-10c section in disk_test_cmds.c):

```c
void hda_print_state(void);
void ac97_print_state(void);
void sb16_print_state(void);
void es1370_print_state(void);
void virtio_snd_print_state(void);
void usb_audio_print_state(void);
```

## Shell commands

| Command    | Purpose                                     |
|------------|---------------------------------------------|
| sound      | list registered sound cards                 |
| hda        | Intel HDA controller/codec status           |
| ac97       | AC'97 controller/codec status               |
| sb16       | Sound Blaster 16 DSP/DMA status             |
| es1370     | ES1370 status                               |
| virtiosnd  | virtio-snd status                           |
| usbaudio   | USB audio + UHCI host status                |
| play       | play a 440 Hz tone (`play [rate]`)          |
| volume     | get/set volume (`volume [0-100]`)           |

`lspci` continues to list PCI devices (sound controllers appear with
class 0x04); `real_hw_test` gained a WP-10c sound section.

## Tests

| Test              | What it verifies                                        |
|-------------------|---------------------------------------------------------|
| hda_test          | HDA init, codec/widget enumeration, DMA play, IRQs      |
| ac97_test         | AC'97 init, BDL DMA play, IRQs                          |
| sb16_test         | DSP reset/version, 16-bit DMA play, block IRQs          |
| es1370_test       | ES1370 init, DAC2 frame DMA play, IRQs                  |
| virtio_snd_test   | virtio-snd init + PCM xfer path (SKIP on QEMU, no model)|
| usb_audio_test    | UAC1 enumeration + ISO OUT packet play                  |
| audio_rw_test     | play/stop round-trip on every registered card           |
| sample_rate_test  | 44100/48000 programming (caps-aware)                    |
| real_hw_test      | hardware inventory (WP-10a/b/c sections)                |

Tests print input / expected / actual and a verdict; families without
hardware in the current environment report SKIPPED (no fake output).

## QEMU environment notes (honest report)

* `-audiodev wav,id=snd0,path=...` writes what the devices play into a
  WAV file - used to verify the end-to-end PCM path.
* QEMU 10.x has **no** `virtio-snd-pci` device model, so virtio_snd is
  exercised only down to "probe finds no device".
* The QEMU-10 `intel-hda` model dead-loops inside its audio voice
  rebuild when the codec format is re-programmed to 44.1 kHz while a
  previous voice exists (reproduced with none/wav backends and with
  hda-output/hda-duplex).  The driver programs the format correctly on
  real hardware; sample_rate_test verifies the HDA 44.1 kHz capability
  bit instead of re-programming it under QEMU.
* A USB audio device left in alt setting 1 while idle disturbs the
  shared audiodev in QEMU; usb_audio therefore activates the streaming
  alternate setting lazily on the first play and drops back to alt 0
  on stop (also the correct USB bandwidth behaviour).
