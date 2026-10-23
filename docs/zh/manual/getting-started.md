<!-- SPDX-License-Identifier: Apache-2.0 -->
# 快速上手

五分钟内从零到 `oc>` 提示符。

## 1. 下载

从[下载页面](https://cubestudio-dev.github.io/OpenCubeOS/)或
[GitHub 发布页](https://github.com/cubestudio-dev/OpenCubeOS/releases)获取当前发布的 ISO：

- `opencube-<version>.iso` - 支持 BIOS+UEFI 混合启动的镜像
  (约 12 MiB)

对照下载页面和发布说明中打印的 SHA256 值进行验证：

```
sha256sum opencube-<version>.iso
```

## 2. 在 QEMU 中启动

```
qemu-system-x86_64 -m 512 -cdrom opencube-<version>.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 \
    -serial mon:stdio
```

你将看到：

1. GRUB 菜单：*Open Cube OS (slot ISO)* 加上强制槽位条目（A / B）- 参见 [boot-modes](boot-modes.md)。它会在 5 秒后自动启动。
2. 内核启动日志：子系统每启动完成一行 `OK`（multiboot2 握手、长模式、帧缓冲、IDT、PIT、键盘、串口、PMM、VMM、堆、驱动、VFS、网络、Shell）。
3. 横幅：`Open Cube OS <version> ready. Type 'help' for commands.`
4. 提示符：`oc> `

使用 UEFI 而非传统 BIOS：

```
qemu-system-x86_64 -m 512 -machine q35 \
    -bios /usr/share/ovmf/OVMF.fd \
    -cdrom opencube-<version>.iso -display none -serial mon:stdio
```

## 3. 首个命令

```
oc> help              (short command list)
oc> help -a           (all 233, alphabetical)
oc> help -w           (grouped by work package)
oc> uname -a          (version + build + tree)
oc> dskstat           (block devices found)
oc> dhcp              (bring up networking)
oc> ping 10.0.2.2     (the QEMU gateway)
oc> wget 10.0.2.2     (fetch the gateway's index page)
oc> dns example.com   (resolve a name through the QEMU DNS forwarder)
oc> health            (CPU / memory / tasks / IO at a glance)
oc> dmesg             (kernel ring buffer)
```

包含每条命令的完整参考文档：
[commands](commands/index.md)。

## 4. 键盘

- Shell 行编辑器支持历史记录（上下键）、`Ctrl+U` 行删除和行内编辑。
- 在真实硬件上，PS/2 键盘直接驱动；通过串口（COM1）输入到 `-serial mon:stdio` 的每个按键都会进入同一个输入队列。
- TUI 层（`tui` 命令）是键盘驱动的：菜单、表单和桌面启动器 - 参见 [tui-guide](tui-guide.md)。

## 5. 文件

启动系统自带一个 `ramfs`（内存文件系统）。你创建的文件在重启后会消失，除非将它们放在已挂载的卷上：

```
oc> dskstat                    (list devices: ata0, usda..., nvme0...)
oc> mkfs.fat32 ata0 confirm    (optional: format a scratch disk)
oc> fatmount ata0 /mnt         (FAT32; exfat/ext4 equivalents exist)
oc> write /mnt/hello.txt "hi"  (a persistent file)
oc> cat /mnt/hello.txt
```

`/etc` 卷是一个小的持久化 FAT32 镜像，系统在存在时会自动挂载 - 它包含 `opencube.conf`、SSH 主机密钥、内核日志和崩溃转储。

## 6. 下一步去哪

- [shell-basics](shell-basics.md) - 管道、重定向、环境变量、别名、脚本
- [network](network.md) - 完整的网络使用指南
- [boot-modes](boot-modes.md) - A/B 槽位和 GRUB 菜单功能
- [update-ota](update-ota.md) - 更新运行中的系统
- [recovery](recovery.md) - WinRE 风格的恢复工作流
- 从源码构建：[../dev/build.md](../dev/build.md)
