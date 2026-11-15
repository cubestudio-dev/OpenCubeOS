<!-- SPDX-License-Identifier: Apache-2.0 -->
# 架构深度解析

| 页面 | 内容概要 |
|------|----------|
| [system-map](system-map.md) | 四个组件（OCBL/OCSE/OCOS/OCIRE）、版本链、物理内存契约、引导模式、A/B |
| [tree-map](tree-map.md) | 目录树的每个目录：里面有什么、为何存在 |
| [hidden-mechanisms](hidden-mechanisms.md) | 横跨文件与组件的十二条承重行为 |
| [private-headers](private-headers.md) | 逐头文件讲解 `ocos/interface/` 的 ring 3 API 契约 |
| [boot-flow](boot-flow.md) | OCBL stage1 -> core -> 扫描器 -> 菜单 -> mb2 交接 -> kmain |
| [memory](memory.md) | PMM、VMM、堆、按缺页增长、DMA 边界、恒等窗口 |
| [scheduler](scheduler.md) | task_t、优先级、轮转、上下文切换 |
| [interrupts](interrupts.md) | IDT/GDT/TSS、IST 纪律、PIC/LAPIC/MSI、异常路径 |
| [syscall-ring3](syscall-ring3.md) | int 0x80 ABI、54 个系统调用的表、SYS_L1SVC、kcap |
| [vfs](vfs.md) | VFS 契约 + 各文件系统的真实行为 |
| [network-stack](network-stack.md) | 逐层剖析，从网卡轮询到 CUBIC/SACK，再到 TLS 与 SSH |
| [driver-model](driver-model.md) | OC_DRIVER_DECL 注册、家族 ops、探测顺序 |
| [tui-engine](tui-engine.md) | 桌面、各页面，以及它们为何绕开 kcap |
| [chinese-layer](chinese-layer.md) | 编码、字体生成、系统 IME 会话协议 |
| [crash-recovery](crash-recovery.md) | panic 路径、OCDUMP01、crashlog 环、看门狗、OCIRE 接管 |
| [ota-ab](ota-ab.md) | 槽位、标志、更新链、8 步安装、回滚 |
