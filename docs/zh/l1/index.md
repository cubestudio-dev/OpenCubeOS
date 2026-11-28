<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 应用开发指南

如何编写、构建并交付 Open Cube OS 的 ring-3 程序 - 从一行服务
包装到一个完整的合并大应用。

一段话讲清应用模型：`ocos/l1apps/<name>/` 之下**一条命令 = 一个
应用 = 一个文件夹**，内含 `main.c`（+ 任何额外 `.c`）与
`i18n/{en,zh}.json`。构建机械化地发现文件夹（没有手写表），把
每个应用以独立环境编译方式对 `ocos/interface/` 编译，并把每个
镜像内嵌进内核的 `OC_USERPROGS[]` 程序注册表（今天有 284 个
程序）。运行时 `ush` 按名字 fork + execve 注册表程序；应用通过
`SYS_L1SVC` 系统调用（类别 + 操作 + 参数行 + 输出缓冲）触达
每个内核服务。

| 页面 | 内容 |
|------|------|
| [overview](overview.md) | 应用模型、分层、数量 |
| [surface-map](surface-map.md) | 每个 L1SVC 类别与操作、系统调用桥 |
| [app-anatomy](app-anatomy.md) | main.c / crt0 / i18n / l1_wizard_api.h - 完整解剖 |
| [writing-apps](writing-apps.md) | 从源码到 `run` 的完整应用 - 教程 |
| [wizard-library](wizard-library.md) | 8 个大应用背后的共享向导原语 |
| [driver-block](driver-block.md) | 扩展块服务 |
| [driver-nic](driver-nic.md) | 扩展网卡服务 |
| [driver-snd](driver-snd.md) | 扩展声音服务 |
| [ota](ota.md) | 应用侧的 OTA/A-B 服务 |
| [solib-loader](solib-loader.md) | 树内 ld.so + ET_DYN 程序 |
| [tui-ext](tui-ext.md) | TUI 服务表面 |
| [watchdog-health](watchdog-health.md) | 应用视角的看门狗与健康 |

## 每个应用提供的三扇门

8 个已合并的应用（`update`、`disk-tool`、`net-tool`、
`sound-tool`、`files`、`sysdiag`、`hwinfo`、`power`）确立了
三门契约 - 无参向导、`direct` 直通、经典操作分发。新应用可以
自由采纳它；向导原语只差一次 `#include "l1_wizard_api.h"`。
