<!-- SPDX-License-Identifier: Apache-2.0 -->
# 启动故障排查

## 启动日志是一份检查单

内核按照点亮顺序（顺序见 [arch/boot-flow.md](../arch/boot-flow.md)）为每个子系统打印一行。诊断方法：

1. **串口完全无输出** - GRUB 未运行或 ISO 条目错误。尝试启动 ISO 的其他槽位条目；比较 BIOS 与 UEFI 模式。
2. **在 `long mode entry OK` 之前停止** - boot.S 阶段：multiboot2 握手失败（magic/mbi 错误）- 使用 `-serial file:` 捕获并检查 mbi 标签解析。
3. **在某个驱动行停止** - 该驱动家族在您的硬件上探测失败；其上一行消息指明了设备。移除该设备（或对应的 QEMU 标志）以确认。
4. **到达 `boot complete` 但无提示符** - Shell 初始化；极其罕见（233 命令注册行会是线索）。
5. **提示符后重启** - 恐慌或看门狗触发：第二次启动后检查 /etc/crashlog（`dumpinfo`、`crashlog`），以及事件期间的串口转储。

## 自测是你的帮手

启动序列已证明：异常路径（3/3）、Shell 注册（233）、崩溃恢复已就绪。如果这些行已打印，说明底层平台正常 - 检查特定功能，而非平台本身。

## 第二次启动分析

A/B 标志与崩溃日志在 /etc 上 survive 重启。任何意外重启后：`dmesg`（当前启动）、`crashlog`/`dumpinfo`（上次启动）、`abcfg`（GRUB 尝试启动的内容）。
