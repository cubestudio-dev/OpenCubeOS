<!-- SPDX-License-Identifier: Apache-2.0 -->
# 启动故障排查

## 启动日志就是检查清单

内核**按上线顺序**每个子系统打印一行（该顺序即
[arch/boot-flow.md](../arch/boot-flow.md)）。诊断：

1. **串口上什么都没有**——OCBL 没有运行，或 ISO 条目不对。
   启动 ISO 的另一个槽位条目；BIOS 与 UEFI 都试试。
2. **停在 `long mode entry OK` 之前**——boot.S 阶段：
   multiboot2 握手失败（魔数/mbi 不对）——用
   `-serial file:` 捕获并检查 mbi 标签解析。
3. **停在某个驱动行**——该家族的探测在你的硬件上失败；其
   上一条消息点名了设备。移除该设备（或 QEMU 参数）以
   确认。
4. **到达 `boot complete` 却没有提示符**——shell 初始化
   问题；极为罕见（程序注册表计数行会是线索）。
5. **提示符之后重启**——panic 或看门狗：第二次启动后检查
   /etc 的 crashlog（`dumpinfo`、`crashlog`），以及事件
   发生期间的串口转储。

## 自检是你的朋友

启动序列已经证明：异常路径 (3/3)、shell 注册 (284)、崩溃恢复
已布防。如果这些行都打印了，底下的机器是健全的——去看具体
特性，而不是平台。

## 第二次启动分析

A/B 标志 + crashlog 在 /etc 上跨重启存活。任何意外复位之后：
`dmesg`（当前启动）、`crashlog`/`dumpinfo`（上一次启动）、
`abcfg`（启动菜单当时试图启动什么）。
