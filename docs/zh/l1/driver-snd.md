<!-- SPDX-License-Identifier: Apache-2.0 -->
# 声音驱动

一个音频家族如何加入声音服务。

## 模型（3.0.0）

- 驱动住在 `ocos/drivers/audio/` - 每个家族一个文件：HDA
  （`drv_audio_hda.c`）、AC'97、SB16、ES1370、virtio-snd；USB
  音频挂在 USB 类驱动上（`drv_usb_audio.c`）。
- 身份：`OC_DRIVER_DECL("snd", "hda", &g_snd_probe_hda,
  "Intel HD Audio")` - 与其他所有类相同的收集器。
- 设备表 + 混音器 + 回放引擎住在声音服务
  （`ocos/services/audio/`）里：设备注册表
  （`driver_snd_device_t`，名字/声道/采样率/音量）、音调合成器
  （`driver_snd_make_tone` - `play` 背后的 440 Hz 测试音）、
  以及 L1SVC 类别 4 表面。

## 服务表面（17 个操作）

`sound`（设备总览）、按驱动的状态（`hda`、`ac97`、`sb16`、
`es1370`、`virtiosnd`、`usbaudio`）、`play [device] [rate]`
（测试音）、`volume [device] [0-100]`，以及自检家族
（`hda_test` .. `sample_rate_test`、RW 回环、采样率精度探针）。

## 代码里携带的防御性注记

- 一张报告 `channels == 0` 的卡不得在帧数行除零（BUG-0243：
  服务报告 0 帧，play 如实报告设备错误）。
- `sound-tool` 向导在数字触达服务之前先做仅数字校验 - 新增
  字段时保持这个分层。

## 一个新家族长什么样

1. `ocos/drivers/audio/drv_audio_<fam>.{c,h}` - 探测、描述符
   解析、PCM 流设置。
2. `OC_DRIVER_DECL("snd", "<fam>", &ops, "description")`。
3. 一个状态操作（只许追加编号）+ `sound-tool` 经典表行 + 按驱动
   的子菜单项。

## 消费者契约

`sound-tool sound` -> `sys_l1svc(SND, SOUND, ...)`；向导与脚本
读英文设备行；`play` 的 usage
（`play [device] [rate 4000-96000]`）是稳定拼法。
