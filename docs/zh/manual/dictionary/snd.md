<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 音频 (17)

### ac97

| 字段 | 值 |
|---|---|
| 名称 | ac97 |
| 分类 | 音频 |
| 功能 | AC97 编解码器状态 |
| 用法 | `ac97` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
ac97
AC'97: not present
ush> 
```

### ac97_test

| 字段 | 值 |
|---|---|
| 名称 | ac97_test |
| 分类 | 音频 |
| 功能 | AC97 播放测试 |
| 用法 | `ac97_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
ac97_test
[ac97_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### audio_rw_test

| 字段 | 值 |
|---|---|
| 名称 | audio_rw_test |
| 分类 | 音频 |
| 功能 | 音频读写测试 |
| 用法 | `audio_rw_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
audio_rw_test
[audio_rw_test] no sound cards registered => SKIPPED
ush> 
```

### es1370

| 字段 | 值 |
|---|---|
| 名称 | es1370 |
| 分类 | 音频 |
| 功能 | ES1370 编解码器状态 |
| 用法 | `es1370` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
es1370
ES1370: not present
ush> 
```

(备注: absent-hardware probe (no ES1370 in this boot))

### es1370_test

| 字段 | 值 |
|---|---|
| 名称 | es1370_test |
| 分类 | 音频 |
| 功能 | ES1370 播放测试 |
| 用法 | `es1370_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
es1370_test
[es1370_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### hda

| 字段 | 值 |
|---|---|
| 名称 | hda |
| 分类 | 音频 |
| 功能 | HD Audio 编解码器状态 |
| 用法 | `hda` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
hda
Intel HDA: not present
ush> 
```

### hda_test

| 字段 | 值 |
|---|---|
| 名称 | hda_test |
| 分类 | 音频 |
| 功能 | HD Audio 播放测试 |
| 用法 | `hda_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
hda_test
[hda_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### play

| 字段 | 值 |
|---|---|
| 名称 | play |
| 分类 | 音频 |
| 功能 | 播放 WAV 文件 |
| 用法 | `play <file>` |
| 参数 | `file` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
play
play: no sound card registered
ush> 
```

### sample_rate_test

| 字段 | 值 |
|---|---|
| 名称 | sample_rate_test |
| 分类 | 音频 |
| 功能 | 采样率测试 |
| 用法 | `sample_rate_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
sample_rate_test
[sample_rate_test] no sound cards registered => SKIPPED
ush> 
```

### sb16

| 字段 | 值 |
|---|---|
| 名称 | sb16 |
| 分类 | 音频 |
| 功能 | 声霸卡 16 状态 |
| 用法 | `sb16` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
sb16
Sound Blaster 16: not present
ush> 
```

### sb16_test

| 字段 | 值 |
|---|---|
| 名称 | sb16_test |
| 分类 | 音频 |
| 功能 | SB16 播放测试 |
| 用法 | `sb16_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
sb16_test
[sb16_test] device not present in this environment => SKIPPED (no ISA SB16 (QEMU: add -device sb16,audiodev=snd0))
ush> 
```

### sound

| 字段 | 值 |
|---|---|
| 名称 | sound |
| 分类 | 音频 |
| 功能 | 音频子系统状态 |
| 用法 | `sound` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
sound
sound devices:
  (none)
ush> 
```

### usb_audio_test

| 字段 | 值 |
|---|---|
| 名称 | usb_audio_test |
| 分类 | 音频 |
| 功能 | USB 音频测试 |
| 用法 | `usb_audio_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
usb_audio_test
[usb_audio_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### usbaudio

| 字段 | 值 |
|---|---|
| 名称 | usbaudio |
| 分类 | 音频 |
| 功能 | USB 音频状态 |
| 用法 | `usbaudio` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
usbaudio
USB Audio: not present
USB: not present
ush> 
```

### virtio_snd_test

| 字段 | 值 |
|---|---|
| 名称 | virtio_snd_test |
| 分类 | 音频 |
| 功能 | virtio 声卡测试 |
| 用法 | `virtio_snd_test` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
virtio_snd_test
[virtio_snd_test] device not present in this environment => SKIPPED (QEMU 10 has no virtio-snd-pci model)
ush> 
```

### virtiosnd

| 字段 | 值 |
|---|---|
| 名称 | virtiosnd |
| 分类 | 音频 |
| 功能 | virtio 声卡状态 |
| 用法 | `virtiosnd` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
virtiosnd
virtio-snd: not present
ush> 
```

### volume

| 字段 | 值 |
|---|---|
| 名称 | volume |
| 分类 | 音频 |
| 功能 | 混音器音量控制 |
| 用法 | `volume [0-100]` |
| 来源 | `ocos/services/audio/src/svc_audio_l1.c` |

**实际执行示例**：

```
volume
volume: no sound card registered
ush> 
```
