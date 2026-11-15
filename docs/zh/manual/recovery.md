<!-- SPDX-License-Identifier: Apache-2.0 -->
# 恢复

## 崩溃产物

内核捕获故障时，会在报告路径之前先写入一条崩溃记录：

```
ush> sysdiag               -> crash records (crashlog + dumpinfo)
ush> crashlog              (the recorded exceptions of this boot)
ush> dumpinfo              (core-dump status)
ush> dmesg                 (the kernel log around the event)
```

`crashtest panic|exc|...`（仅 classic/direct——刻意不设为向导按钮）注入受控故障来演练这套机制。

## A/B 逃生通道

损坏的槽位绝不会让机器变砖：

1. 重启；GRUB 菜单提供强制槽位 A / 强制槽位 B 项。
2. 引导另一个槽位，然后在其中执行：

```
ush> update rollback
```

## WinRE 风格的恢复流程

1. 引导任意 Open Cube OS ISO（救援工具包就是安装程序）。
2. 接上故障盘（`-drive if=ide,file=broken.img`）。
3. 只查看、不写入：

```
ush> disk-tool               lsblk / parted / fsck - all read-only here
ush> files                   browse the damaged volume once mounted
```

4. 用 `files` 恢复数据（cp 到一块已挂载的 FAT32 卷）。
5. 必要时重装：`install <dev>`（单系统）或 `disk-tool` 的 A/B 管理器（更新盘形态）。

## 崩溃转储子系统

内核核心转储会落入崩溃服务的产物区域，带寄存器与栈状态；`dumpinfo` 报告其位置。[../internals/crash-dump.md](../internals/crash-dump.md) 页面介绍了格式与保留策略。

## 看门狗

看门狗服务守护系统存活；卡死的内核会触发复位，下次启动的 crashlog 会显示看门狗事件及卡死上下文。`watchdog`（classic / `sysdiag` 表）报告状态；内部机制见 [../internals/watchdog.md](../internals/watchdog.md)。
