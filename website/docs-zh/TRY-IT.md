<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - 上手试用指南（TRY-IT 中文版）

内核的每一项硬件功能，附挂载该设备的确切 QEMU 命令、在客户机内
练习该功能的确切命令，以及你应当看到的输出。如果你的输出不一致，
最后一节列出了常见原因。

本指南覆盖**存储、网络、声卡**。系统内更新（OTA）流程有专门的
分步指南：**docs/UPDATE-HOWTO.md**（中文翻译见本页 UPDATE-HOWTO 节）。

适用于全文的说明：

- 下文使用的 `tools/qemu_runner.py` 脚本会引导 ISO 并替你输入客户机
  命令（单次会话，串口控制台）。你也可以 `make run-bios` 引导后自己
  输入命令。
- `mkdir`、`touch`、`rm`、`cp`、`mv` 成功时不输出（Unix 惯例）；
  失败时总会打印原因。
- `run <program>` 启动用户程序；其输出可能出现在 `oc>` 提示符之后，
  因为程序与 shell 并发运行。输出一定会在你输入的下一条命令结束前
  到达。

## 0. 你需要什么

| 项目                    | 来源                                    |
|-------------------------|-----------------------------------------|
| Open Cube OS ISO        | GitHub Releases / 本站                  |
| QEMU（6.0+）            | 发行版软件包                            |
| 一块空白 raw 磁盘镜像   | `dd if=/dev/zero of=disk.img bs=1M count=64` |

## 1. 存储（WP-10a）

在 IDE 总线上挂一块空白磁盘：

```sh
dd if=/dev/zero of=disk.img bs=1M count=64
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=disk.img
```

客户机内——格式化、挂载、写入、读取、检查、卸载：

```
oc> lsblk
Block devices:
  hda [ATA] 131072 sectors (65536 KiB) sector_size=512
oc> mkfs.fat32 hda
FAT32 formatted on hda
oc> fatmount hda /mnt
vfs: mounted fat32 at /mnt
fat32 mounted. Try: ls /mnt
oc> write /mnt/note.txt audit test file
wrote 15 bytes
oc> cat /mnt/note.txt
audit test file
oc> df
Filesystem     Mount     Device    Use
ramfs         /        -        7 nodes, 485 bytes
fat32         /mnt     hda      129020 free clusters, 512 bytes/cluster
oc> umount /mnt
umounted /mnt
oc> fsck hda
fsck: FAT32 filesystem on hda
  ...
  fsck: PASS (basic checks OK)
```

`mkfs.exfat` 与 `mkfs.ext4` 同样可用；`fsck` 会如实报告卷类型
（"ext2/3/4 volume detected ... fsck supports FAT32 only"），而不会
误解析。

其他控制器——用下面任意一条替代（或追加）上面的 IDE 盘：

```sh
# AHCI（SATA）
-device ich9-ahci,id=ahci -drive if=none,id=da,file=disk.img,format=raw \
-device ide-hd,drive=da,bus=ahci.0
# NVMe
-device nvme,id=nv0,serial=oc -drive if=none,id=dn,file=disk.img,format=raw \
-device nvme-ns,drive=dn
# virtio-blk
-drive if=none,id=dv,file=disk.img,format=raw -device virtio-blk-pci,drive=dv
```

磁盘会以 `sda`（AHCI）、`nvme0`（NVMe）或 `vda`（virtio）出现在
`lsblk` 中；每类都有自测命令（`ahci_test`、`nvme_test`、
`virtio_blk_test`、`ata_dma_test`、`disk_rw_test <dev>`、
`fs_mount_test <dev>`、`partition_test`），打印输入 / 预期 / 实际与
判定。

## 2. 网络（WP-10b）

默认的 `qemu_runner.py` 会话已经带一块 e1000 网卡。普通 QEMU 运行
请追加：

```sh
-netdev user,id=n1 -device e1000,netdev=n1
```

客户机内——DHCP、接口、ping、DNS、路由：

```
oc> dhcp
DHCP success: IP=10.0.2.15
  mask=255.255.255.0
  gateway=10.0.2.2
  DNS=10.0.2.3
oc> ifconfig
e1000:
  HWaddr 52:54:00:12:34:56
  inet 10.0.2.15
  ...
  link: UP
oc> ping 10.0.2.2
PING 10.0.2.2 ...
  reply received
  reply received
  reply received
  reply received
oc> dns example.com
A: 104.20.23.154
oc> route
Routing table:
  0.0.0.0 mask=0.0.0.0 gw=10.0.2.2 (default)
```

其他驱动族（每族都有对应的 `*_test` 自测）：

```sh
-device e1000e,netdev=n1    # 然后运行：e1000e_test
-device igb,netdev=n1       # 然后运行：igb_test
-device rtl8139,netdev=n1   # 然后运行：rtl8139_test
```

（每条前面都要加 `-netdev user,id=n1`。）没有 QEMU 设备模型的族
（ixgbe、bcm57xx 等）会带原因如实报告 `SKIPPED`——设备不存在时
绝不伪造输出。

## 3. 声卡（WP-10c）

挂一张 Intel HDA 声卡（现实中最常见的控制器）。QEMU 10 需要显式
`audiodev`；`wav` 后端会把内核播放的内容写入 WAV 文件，方便你在
宿主机上核对 PCM 通路：

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -audiodev wav,id=snd0,path=/tmp/out.wav \
  -device intel-hda -device hda-output,audiodev=snd0
```

客户机内——列出声卡、状态、音量、播放：

```
oc> sound
sound devices:
  0: hda (hda) rate=48000 ch=2 vol=74 played=0B
oc> hda
Intel HDA: vid=0x8086 did=0x2668 at 00:04.0 codec=00 dac_nid=02 ...
oc> volume 60
volume: hda = 60
oc> volume
volume: hda = 60
oc> play
play: hda 440Hz 48000 frames @ 48000Hz
```

如果设备名写错，命令会告诉你可用的名字：

```
oc> play usbaudio
play: unknown device 'usbaudio' (available: hda)
usage: play [device] [rate 4000-96000]
```

一台机器挂更多声卡（全部五族）：

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -audiodev wav,id=snd0,path=/tmp/out.wav \
  -device intel-hda -device hda-output,audiodev=snd0 \
  -device AC97,audiodev=snd0 -device sb16,audiodev=snd0 \
  -device ES1370,audiodev=snd0 \
  -device piix3-usb-uhci -device usb-audio,audiodev=snd0
```

此时 `sound` 列出五张卡；`play <name>` 与
`volume <name> 0-100` 按卡生效（`hda`、`ac97`、`es1370`、
`usb-audio`、`sb16`——与 `sound` 显示的名字完全一致）。自测：
`hda_test`、`ac97_test`、`sb16_test`、`es1370_test`、
`usb_audio_test`，另有 `audio_rw_test`（每张卡播放/停止往返）与
`sample_rate_test`（44.1/48 kHz）。

说明：
- QEMU 10 **没有** `virtio-snd-pci` 模型，所以 `virtio_snd_test`
  在 QEMU 10 下如实报告 SKIPPED；在真实 virtio 硬件上会运行。
- QEMU 10 的 `intel-hda` 模型在重编程到 44.1 kHz 时会死循环；
  `sample_rate_test` 因此改为验证能力位（见
  docs/EXTENSIONS_WP10c.md）。

## 4. 更新 / 回滚（WP-10u）

带预期输出的完整分步指南：**docs/UPDATE-HOWTO.md**。简版：

```sh
bash tools/make_ab_disk.sh build/opencube.elf   # 生成 build/abdisk.img
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=build/abdisk.img \
  -netdev user,id=n1 -device e1000,netdev=n1
```

```
oc> update --status          # A/B 磁盘：在位
oc> ab_partition_test        # 7/7 PASS
oc> update                   # 下载、校验、安装到 slot B
oc> reboot                   # 自动引导 slot B
oc> rollback                 # 下次引导回到 slot A
```

## 5. 无需任何硬件

以下命令在裸 QEMU 会话（不挂任何额外设备）即可使用：

| 试试这个                          | 你应当看到                              |
|-----------------------------------|-----------------------------------------|
| `help`                            | 146 条命令的列表                        |
| `uname -a`                        | `Open Cube OS WP-10c x86_64`            |
| `mkdir /d` + `write /d/f hi` + `cat /d/f` | `hi`                            |
| `mem`、`heap`、`ps`、`sched`      | 内存/任务/调度器统计                    |
| `heaptest`、`memtest`、`vmtest`   | PASS                                    |
| `run hello`                       | `hello from userspace`                  |
| `run fork_test` / `run pipe_test` / `run signal_test` | ... PASS           |
| `cryptotest`                      | SHA-256 / AES / HMAC 3/3 PASS           |
| `crashlog`                        | 最近的内核异常（如有）                  |

## 故障排查

| 症状                                      | 原因 / 处理                                 |
|-------------------------------------------|---------------------------------------------|
| `mkfs.fat32: device not found`            | 设备名不对；先运行 `lsblk`                  |
| `fsck: cannot read boot sector`           | 磁盘从未格式化                              |
| `checkupdate` 后 `connect failed`         | 无网络：先运行 `dhcp`                       |
| `dhcp` 只打印 discover 没有租约           | 缺网卡：加 `-netdev user,id=n1 -device e1000,netdev=n1` |
| `play: no sound card registered`          | 无音频设备：见第 3 节                       |
| QEMU 10 拒绝音频参数                      | 每个音频设备都需要 `audiodev=snd0`          |
| `SKIPPED` / `FAIL (no ... registered)`    | 对缺席硬件的如实报告，不是 bug              |
