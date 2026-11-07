<!-- SPDX-License-Identifier: Apache-2.0 -->
# 声卡点亮手册

## 家族

| 驱动 | QEMU | 命令 | 测试 |
|--------|------|---------|------|
| HDA | `-device intel-hda -device hda-output` | `hda` | `hda_test` |
| AC97 | `-device AC97` | `ac97` | `ac97_test` |
| SB16 | `-device sb16` (ISA) | `sb16` | `sb16_test` |
| ES1370 | `-device es1370` | `es1370` | `es1370_test` |
| virtio-snd | `-device virtio-sound` | `virtiosnd` | `virtio_snd_test` |

## 点亮顺序

1. `sound` - 家族状态 + 已注册设备。
2. 各家族命令显示编解码器/混音器/DSP 状态。
3. `play` - 通过默认设备播放一个音调。
4. 门禁：各家族的 `*_test` + `sample_rate_test` + `audio_rw_test`。

## L1 接口面

`driver_snd_register` + play/stop/set_rate/set_volume/get_caps；详见
[l1/driver-snd.md](../l1/driver-snd.md)。
