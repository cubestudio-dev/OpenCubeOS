<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS 文档

欢迎使用 Open Cube OS 文档中心 - 这是一套从头编写、直接启动进 Ring 3 的 x86_64 内核（L0 是纯服务内核；整个用户世界 —— ush Shell、TUI 桌面、中文控制台 —— 都在 `src/L1/` 下的 L1 里）。

所有内容均基于真实代码库编写：命令计数、接口表和行统计均为机器生成（`make api-docs` -> `docs/data/*.json`），且手册页引用了实现其描述内容的文件。

## 从这里开始

- 系统新手：[快速上手](manual/getting-started.md)
- 想要构建：[开发环境](dev/environment.md)
    然后 [BUILDING.md](root/BUILDING.md)
- 想要扩展：[L1 扩展指南](l1/overview.md)
- 架构问题：[Open Cube OS 架构](root/ARCHITECTURE.md) 然后
    查看 [arch/](arch/) 下的深度解析
- 遇到问题：[故障排查](troubleshooting/common-errors.md)

## 文档库

| 章节 | 内容 |
|---------|----------|
| [manual/](manual/) | 用户手册：启动、Shell、文件、网络、TUI、OTA、恢复 + 全部 233 条命令页面 |
| [dev/](dev/) | 开发者手册：环境、构建、QEMU、调试、测试、PR 规范、可移植性、风格 |
| [arch/](arch/) | 架构深度解析：启动流程、内存、调度器、中断、Ring 3、VFS、网络、驱动、TUI |
| [l1/](l1/) | L1 扩展指南：接口面、驱动家族、编写模块、共享对象加载器 |
| [drivers/](drivers/) | 驱动点亮手册：16 个家族 |
| [troubleshooting/](troubleshooting/) | 错误目录和 QEMU 调试 |
| [internals/](internals/) | 子系统内部机制：看门狗、klog、健康监控、崩溃转储、加密核心、OTA/A-B |
| [release/](release/) | 发布工程：流程 SOP、版本号、官网 + Wiki 同步 |

## 快速数据（3.0.0-WP06c）

- 285 条 Shell 命令 —— 274 个 Ring 3 注册表应用 + 11 个 ush 内建，L0 里零条命令（在访客系统中运行 `help -a`）
- 20 个 SYS_L1SVC 服务类别 / 190 个编号条目（INTERFACES.md，l1svc_defs.h）
- 54 个系统调用（`src/L0/services/sysapi/core_syscall.h`）
- 128,890 行原创源代码，624 个文件
- Apache 2.0 许可，每个文件均带有 SPDX 标签
