<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI 桌面

## 启动桌面

```
ush> tui
```

桌面会占用图形控制台（QEMU 需以 `-vga std` 运行）；串口保留 ush 提示符，可并行访问。

## 屏幕布局

- 顶部**面板**：时钟、电源、系统摘要
- 中部**桌面**：应用启动器
- **任务栏**：运行中的 TUI 应用、启动器按钮

键盘模型：方向键 + Enter 导航，ESC 后退，菜单键打开启动器。不强制使用鼠标——每个操作都能从键盘触达（这是无障碍门禁所断言的输入契约）。

## 内置 TUI 应用

| 应用 | 功能 |
|-----|--------------|
| tui-files | 浏览 / 复制 / 移动 / 删除、启动 edit |
| tui-editor | `edit` 命令所用的同一编辑器内核，带边框 |
| tui-proc | 进程表（`ps` 的数据） |
| tui-apps | 应用启动器 / 注册表浏览器 |
| tui-time | 时钟（RTC / HPET / TSC） |
| tui-net | 网络状态界面 |
| tui-sysinfo / tui-hw / tui-drv / tui-sys | 硬件 + 系统页面 |

## TUI 命令家族

`tui`（桌面）、`tui-test`（自检）、`tui-hw`、`tui-net`、`tui-sysinfo [page]`、`tui-time`、`tui-drv [page]`、`tui-sys [page]`、`tui-ext`——全部是注册表程序，桌面运行期间也都可通过 shell 调用。

## TUI 与大应用

TUI 呈现的是 8 个大应用所包装的同一批服务——sysinfo 页面读取的 L1SVC 操作与 `hwinfo` 相同。桌面是查看器；向导应用是操作员界面；经典命令是脚本界面——三扇门，一个系统。
