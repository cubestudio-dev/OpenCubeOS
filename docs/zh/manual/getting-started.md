<!-- SPDX-License-Identifier: Apache-2.0 -->
# 快速上手

## 启动 ISO

最快的路径是用 QEMU 运行发布 ISO：

```
qemu-system-x86_64 -m 512M -cdrom opencube-3.0.0.iso -boot d \
    -netdev user,id=n1 -device e1000,netdev=n1
```

加 `-vga std` 可获得图形控制台（TUI 桌面需要它），或用 `-display none -serial mon:stdio` 以无头模式运行。在 UEFI 主机上再加 `-bios OVMF.fd`（见 [boot-modes](boot-modes.md)）。

启动自检运行完毕，随后 ring-3 shell 与你见面：

```
Open Cube OS User-space Shell (3.0.0)
ush>
```

## 头五分钟

8 个大应用是预期的操作面——从一个向导开始，看它干活：

```
ush> hwinfo            what hardware is in this machine
ush> sysdiag           health snapshot, kernel log, memory picture
ush> net-tool          address setup, WiFi scan, connectivity tests
ush> update            the 5-step OTA wizard (network -> A/B -> DNS
                        -> check -> install)
```

按数字键选择菜单项，`q` 退出。破坏性路径会要求你重新输入目标名称，并回答一个单独的 `y`。

两个快速的只读导览：

```
ush> disk-tool             disks, partitions, mounts
ush> files                 browse, copy, edit
```

## 经典命令依然可用

每条历史命令都保持注册——接口冻结规则保证了这一点（命令只增不删；注册表收录 284 个程序）：

```
ush> lsblk                 same service call as disk-tool lsblk
ush> checkupdate           same as update's step 4
ush> help                  shell built-ins + registry summary
ush> help -a               every registry entry, A-Z, tagged
ush> help -w               grouped by domain (the mega group first)
```

## 文件

```
ush> files                 the wizard: ls / tree / df+du / cp / mv /
                            rm / edit / stat+chmod
ush> files edit /etc/opencube.conf      the full-screen editor
ush> cat /etc/opencube.conf              quick read
ush> cd /etc && ls
```

ISO 从 RAM 运行（ramfs 根）；接入一块磁盘并用 `disk-tool` 分区 + 格式化 + 挂载即可获得持久化（见 [filesystems](filesystems.md)）。

## 网络

使用上面的用户态网卡时：

```
ush> dhcp                  or net-tool -> "obtain an address via DHCP"
ush> ping 10.0.2.2         the QEMU gateway
ush> dns example.com
ush> wget example.com 80 /
```

TLS 与 SSH 均为内置：`wget` 协商 HTTPS，`sshd` 提供 shell 服务，`ssh` 向外连接（见 [network](network.md)）。

## 关机

```
ush> power                 the wizard (confirmed shutdown/reboot)
ush> poweroff              the classic one-shot
```

## 接下来去哪里

- [mega-apps](mega-apps.md)——向导 / direct / classic 契约
- [shell-basics](shell-basics.md)——管道、重定向、脚本
- [update-ota](update-ota.md)——更新向导与 A/B 机制
- [chinese-console](chinese-console.md)——`lang zh` 与输入法
