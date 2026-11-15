<!-- SPDX-License-Identifier: Apache-2.0 -->
# 启动模式

## BIOS（传统）

默认方式：从 ISO 引导 GRUB i386-pc，加载 multiboot2 内核及其内嵌模块链。`install` / `abdisk` 写入磁盘的正是同一个加载器，因此已安装系统的引导方式与其来源 ISO 完全一致。

## UEFI

ISO 自带一条 EFI 启动分支；在 QEMU 下用 OVMF 引导它：

```
qemu-system-x86_64 -m 512M -bios OVMF.fd -cdrom opencube-3.0.0.iso \
    -boot d
```

在真实硬件上，于固件启动菜单中选择 EFI 项。A/B 槽位逻辑（见下文）在两种模式下完全相同。

## A/B 槽位菜单

在 A/B 盘上，GRUB 读取启动标志并提供：

- **活动槽位**（默认，自动选中）；
- **强制槽位 A** 与**强制槽位 B** 项——活动槽位表现异常时的手动覆盖手段。

标志保存在 A/B 盘上；`update status` 显示哪个槽位正在运行、哪个槽位下次启动，`update rollback` 可在系统内部将其翻转。

## 持久化

ISO 引导的系统从 RAM 运行：重启后一切重置。要获得持久化的系统：

1. `install <disk>`——分区 + 复制当前运行的内核 + GRUB + /etc 卷，单系统形态；或
2. `abdisk <disk>`（通过 `disk-tool` 的 A/B 管理器）——双槽位更新盘形态，OTA 更新以它为目标。

挂载一个配置卷（`etc.img`，一块包含 `opencube.conf` 与可选 `ssh_host_key` 的 FAT32 磁盘），即可在不安装的情况下跨重启携带设置与主机密钥。

## 启动自检

每次引导都会在 shell 之前运行自检链：

```
exception self-test: 3/3
boot complete....... OK
```

一次到达 `ush>` 的引导，已经证明了它的异常栈、定时器和控制台。回归门禁会断言这些相同的输出行，外加一次实时帧缓冲检查（BUG #66）——所以"它启动成功了"是一句经过测试的陈述，而不是一个愿望。
