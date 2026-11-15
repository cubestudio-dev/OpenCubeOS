<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open Cube OS 文档

欢迎使用 Open Cube OS 文档中心 - 这是一套从头编写、直接启动进
Ring 3 的 x86_64 内核（L0 是纯服务内核；整个用户世界 —— ush
Shell、TUI 桌面、中文控制台、8 个融合大应用 —— 都在
`ocos/l1apps/` 下的 L1 里）。

所有内容均基于真实代码库编写：命令计数、接口表和行统计均为
机器生成（`make api-docs` -> `docs/data/*.json`），且手册页引用
了实现其描述内容的文件。

## 从这里开始

- 系统新手：[快速上手](manual/getting-started.md)
- 命令面：[八大融合应用](manual/mega-apps.md)
- 想要构建：[开发环境](dev/environment.md)
    然后 [BUILDING.md](root/BUILDING.md)
- 想要扩展：[Ring-3 应用开发](l1/overview.md)
- 架构问题：[ARCHITECTURE.md](root/ARCHITECTURE.md) 然后
    [arch/](arch/) 下的深度剖析
- 出了问题：[问题排查](troubleshooting/common-errors.md)
- 融合应用的故事：[3.0.0-WP-l1apps-merge](release/3.0.0-WP-l1apps-merge.md)

## 文档库

| 板块 | 内容 |
|------|------|
| [manual/](manual/) | 用户手册：大应用、启动、Shell、文件、网络、TUI、OTA、恢复 + 全部 292 个命令参考页 |
| [dev/](dev/) | 开发者手册：环境、构建、QEMU、调试、测试、PR 门槛、可移植性、风格 |
| [arch/](arch/) | 架构深度剖析：启动流、内存、调度器、中断、Ring 3、VFS、网络、驱动、TUI |
| [l1/](l1/) | Ring-3 应用开发：接口面地图、应用解剖、向导库、应用编写教程、服务家族 |
| [drivers/](drivers/) | 驱动上手手册：16 个家族 |
| [troubleshooting/](troubleshooting/) | 错误目录与 QEMU 调试 |
| [internals/](internals/) | 子系统内幕：看门狗、klog、健康、崩溃转储、密码学种子、OTA/A-B |
| [release/](release/) | 发布工程：流程 SOP、版本号、网站与 Wiki 同步 |

## 速览（3.0.0-WP-l1apps-merge）

- 296 条 Shell 命令 —— 284 个 Ring-3 注册表应用（含 8 个融合
  大应用）+ 12 个 ush 内建，L0 零命令（guest 内 `help -a`）
- 20 个 SYS_L1SVC 服务类别 / 208 个编号操作
  （INTERFACES.md、l1svc_defs.h）
- 54 个系统调用（`ocos/services/sysapi/interface/svc_sysapi_syscall.h`）
- 158,474 行原创源码，764 个文件
- Apache 2.0，每个文件带 SPDX 标签
