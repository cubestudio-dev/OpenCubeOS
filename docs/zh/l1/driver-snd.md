<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：声卡驱动

## 接口面

`drivers/snd/driver_snd.h`（28 个声明；其中编号的为 26 个）：

```
driver_snd_register(name, ops, ctx)
driver_snd_play(dev, samples, count)
driver_snd_stop(dev)
driver_snd_set_rate(dev, hz)
driver_snd_set_volume(dev, pct)
driver_snd_get_caps(dev, out)
```

外加 `driver_snd_probe_all`、`driver_snd_make_tone`、各硬件的初始化入口（hda/ac97/sb16/es1370/virtio）以及查找/列举辅助函数。

## 各家族点亮要点

- **HDA**：编解码器 + 流，组件图。
- **AC97**：混音器寄存器，AC-link 时序。
- **SB16**：DSP 命令 + DMA16。
- **ES1370**：混合播放通道。
- **virtio-snd**：virtqueue 协议（以及 QEMU 中的 `-device virtio-sound`）。

`play`（播放音调）、`sound`（状态）、`*_test` 命令，以及 `virtio_snd_test`/`sample_rate_test` 是你的门禁。ISO 端点对其尾部进行零填充（无越界读取 - 音频审计已修复该问题）。
