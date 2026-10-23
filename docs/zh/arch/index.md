<!-- SPDX-License-Identifier: Apache-2.0 -->
# 架构深度解析

| 页面 | 内容概要 |
|------|----------|
| [boot-flow](boot-flow.md) | GRUB -> boot.S -> mb2 -> kmain，两阶段链接 |
| [memory](memory.md) | PMM、VMM、堆、按缺页增长、DMA 边界 |
| [scheduler](scheduler.md) | 模型、节拍、策略看门狗、信号 |
| [interrupts](interrupts.md) | IDT/IST、PIC/LAPIC、异常处理路径 |
| [syscall-ring3](syscall-ring3.md) | ABI、ELF 加载、进程、ld.so |
| [vfs](vfs.md) | VFS 契约 + 各文件系统的实际行为 |
| [network-stack](network-stack.md) | 分层架构，从网卡驱动到 TLS/SSH |
| [driver-model](driver-model.md) | 注册接口面、PCI/USB 核心 |
| [tui-engine](tui-engine.md) | 窗口、控件、桌面、桥接 |
| [chinese-layer](chinese-layer.md) | 字体/编码/IME 组件，兼顾两种环境 |
| [crash-recovery](crash-recovery.md) | panic 路径、IST 设计原理、姿态 |
| [ota-ab](ota-ab.md) | 槽位、标志位、更新链 |
