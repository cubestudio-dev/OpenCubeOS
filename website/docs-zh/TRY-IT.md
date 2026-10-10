<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/TRY-IT.md (website-provided, for reading
     convenience). The English original in the repository is authoritative.
     Commands, outputs, paths and identifiers are kept verbatim.
     Structurally re-aligned with the current English original at
     WP-10-AUDIT_P2-fix3 (fix3 G5, BUG-0270). -->

# Open Cube OS - 上手指南

内核的每一项硬件特性，附上挂载设备的准确 QEMU 命令与在客户机内
操作它的准确命令——包括你应该看到的输出。如果你的输出不同，
最后一节列出了常见原因。

本指南覆盖**存储、网络与声卡**。系统内更新（OTA）流程有专门的
分步指南：**docs/UPDATE-HOWTO.md**。

通篇适用的注意事项：

- 下文使用的 `tools/qemu_runner.py` 会启动 ISO 并替你输入客户机命令
  （单会话、串口控制台）。你也可以 `make run-bios` 启动后自己敲。
- `mkdir`、`touch`、`rm`、`cp`、`mv` 成功时是静默的（Unix 惯例）。
  失败时总会打印原因。
- `run <program>` 启动用户程序；因为程序与 shell 并发运行，其输出
  可能出现在 `oc>` 提示符之后。输出一定会在你输入的下一条命令结束
  之前到达。

## 0. 你需要什么

| 项目                    | 位置                                    |
|-------------------------|-----------------------------------------|
| Open Cube OS ISO        | GitHub Releases / 官网                  |
| QEMU (6.0+)             | 发行版软件包                            |
| 一个空白 raw 磁盘镜像   | `dd if=/dev/zero of=disk.img bs=1M count=64` |

## 1. 存储（WP-10a）

在 IDE 总线上挂一块空盘：

```sh
dd if=/dev/zero of=disk.img bs=1M count=64
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=disk.img
```

在客户机内——格式化、挂载、写、读、检查、卸载：

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

`mkfs.exfat` 与 `mkfs.ext4` 也能用；`fsck` 会如实报告那些卷类型
（"ext2/3/4 volume detected ... fsck supports FAT32 only"），
而不是错误解析。

其他控制器——用下面任意一条替代（或并列于）IDE 盘：

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

磁盘在 `lsblk` 里显示为 `sda`（AHCI）、`nvme0`（NVMe）或 `vda`
（virtio）；每类都有自测（`ahci_test`、`nvme_test`、
`virtio_blk_test`、`ata_dma_test`、`disk_rw_test <dev>`、
`fs_mount_test <dev>`、`partition_test`），打印输入 / 预期 / 实际
与判定。

## 2. 网络（WP-10b）

默认 `qemu_runner.py` 会话已带一张 e1000 网卡。裸 QEMU 运行请加：

```sh
-netdev user,id=n1 -device e1000,netdev=n1
```

在客户机内——DHCP、接口、ping、DNS、路由：

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

其他驱动族（每族有对应的 `*_test` 自测）：

```sh
-device e1000e,netdev=n1    # 然后运行：e1000e_test
-device igb,netdev=n1       # 然后运行：igb_test
-device rtl8139,netdev=n1   # 然后运行：rtl8139_test
```

（每条前面都要加 `-netdev user,id=n1`。）没有 QEMU 型号的族
（ixgbe、bcm57xx、……）会带原因报告 `SKIPPED`——不会为缺席设备
伪造输出。

## 3. 声卡（WP-10c）

挂一张 Intel HDA 卡（现实中最常见的控制器）。QEMU 10 需要显式
`audiodev`；`wav` 后端把内核播放的内容写入 WAV 文件，方便你在
宿主机上核对 PCM 路径：

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -audiodev wav,id=snd0,path=/tmp/out.wav \
  -device intel-hda -device hda-output,audiodev=snd0
```

在客户机内——列卡、状态、音量、播放：

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

设备名写错时，命令会告诉你可用的名字：

```
oc> play usbaudio
play: unknown device 'usbaudio' (available: hda)
usage: play [device] [rate 4000-96000]
```

一台机器挂多张卡（全部五族）：

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -audiodev wav,id=snd0,path=/tmp/out.wav \
  -device intel-hda -device hda-output,audiodev=snd0 \
  -device AC97,audiodev=snd0 -device sb16,audiodev=snd0 \
  -device ES1370,audiodev=snd0 \
  -device piix3-usb-uhci -device usb-audio,audiodev=snd0
```

`sound` 会列出五张卡；`play <name>` 与
`volume <name> 0-100` 按卡生效（`hda`、`ac97`、`es1370`、
`usb-audio`、`sb16`——与 `sound` 显示的名字一致）。自测：
`hda_test`、`ac97_test`、`sb16_test`、`es1370_test`、
`usb_audio_test`，另有 `audio_rw_test`（每张卡的播放/停止往返）
与 `sample_rate_test`（44.1/48 kHz）。

注意：
- QEMU 10 **没有** `virtio-snd-pci` 型号，`virtio_snd_test` 在那里
  报告 SKIPPED；在真实 virtio 硬件上它会运行。
- QEMU 10 的 `intel-hda` 型号在重编程为 44.1 kHz 时会死循环；
  `sample_rate_test` 改为验证能力位
  （见 docs/EXTENSIONS_WP10c.md）。

## 4. 更新 / 回滚（WP-10u）

带预期输出的完整分步指南：**docs/UPDATE-HOWTO.md**。简版——
**在系统内**创建 A/B 盘（第 ⑨ 条自宿主；无需宿主机脚本）：

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=abdisk.img \
  -netdev user,id=n1 -device e1000,netdev=n1
```

```
oc> abdisk hda               # 创建 A/B 布局 + GRUB + slot A 内核
oc> update --status          # A/B 盘：present（无需重启）
oc> ab_partition_test        # 7/7 PASS
oc> dhcp && config set update_url http://10.0.2.2:8008/update.json
oc> update                   # 下载、校验、安装到 slot B
oc> reboot                   # 自动引导 slot B
oc> rollback                 # 下次启动回 slot A
```

`reboot` 之后，GRUB 从 p1 上的标志文件选择槽位
（`BOOT_SLOT=B`），系统完整起来后更新即被确认
（`slot B boot confirmed`）。**拔掉 ISO** 重启，磁盘自己就能引导
——`abdisk` 把 GRUB BIOS 引导器写进 MBR 间隙
（boot.img -> LBA0，core.img -> LBA1..）。

## 4b. 把系统安装到磁盘（单系统）

```sh
qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d \
  -drive if=ide,format=raw,file=disk.img
```

```
oc> install hda              # 分区 + FAT32 + 内核 + GRUB
oc> sync                     # 冲刷磁盘缓存
```

去掉 ISO 重启（`-boot c`），机器直接从磁盘引导 Open Cube OS。
`grub-install <dev>` 只向已分区的盘写引导器（MBR 间隙，或 GPT
BIOS boot 分区）。`abcfg` 打印 abdisk/install 写入的 grub.cfg。
注意：这些命令安装的是 **BIOS** 引导路径；UEFI 机器请从 ISO 引导
安装器（ISO 本身 BIOS+UEFI 双引导）。

## 5. 无需任何硬件

裸 QEMU 会话、不加任何设备就能试的：

| 试试这个                          | 你应该看到                              |
|-----------------------------------|-----------------------------------------|
| `help`                            | 150 条命令的列表                        |
| `uname -a`                        | `Open Cube OS <version> x86_64`，其中 `<version>` 是构建期 `git describe --tags` 串（例如 `Open Cube OS WP-10-AUDIT_P2-fix2b-7-g98a8dc7 x86_64`；Makefile `OC_RELEASE_VERSION`） |
| `mkdir /d` + `write /d/f hi` + `cat /d/f` | `hi`                            |
| `mem`、`heap`、`ps`、`sched`      | 内存/任务/调度器统计                    |
| `heaptest`、`memtest`、`vmtest`   | PASS                                    |
| `run hello`                       | `hello from userspace`                  |
| `run fork_test` / `run pipe_test` / `run signal_test` | ... PASS           |
| `cryptotest`                      | SHA-256 / AES / HMAC 3/3 PASS           |
| `crashlog`                        | 最近的内核异常（若有）                  |

## 故障排查

| 症状                                       | 原因 / 修复                                 |
|--------------------------------------------|---------------------------------------------|
| `mkfs.fat32: device not found`             | 设备名不对；先运行 `lsblk`                  |
| `fsck: cannot read boot sector`            | 该盘从未格式化                              |
| `checkupdate` 后 `connect failed`          | 无网络：先运行 `dhcp`                       |
| `dhcp` 打印 discover 但没有 lease          | 缺网卡：加 `-netdev user,id=n1 -device e1000,netdev=n1` |
| `play: no sound card registered`           | 无音频设备：见第 3 节                       |
| QEMU 10 拒绝音频参数                       | 每个音频设备都需要 `audiodev=snd0`          |
| `SKIPPED` / `FAIL (no ... registered)`     | 对缺席硬件的如实报告，不是 bug              |
