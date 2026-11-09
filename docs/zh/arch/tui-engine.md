<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI 引擎

`tui/` 是一个在帧缓冲控制台上的窗口系统：

## 核心

`tui_core.c`（事件循环 + 窗口管理器）、`tui_win.c`（窗口）、`tui_menu.c`、`tui_form.c`、`tui_panel.c`、`tui_text.c`（控件）、`tui_input.c`（键盘路由）、`tui_hw.c`（渲染诊断）。

## 桌面

`tui_desktop.c` + `tui_dt_*`：任务栏、启动器、应用程序（文件、编辑器、进程/系统监控器、网络状态、系统信息、时钟）。

## 命令桥

`tui_cmd.c` 通过 L1 API 枚举 shell 命令表（任何 L1 模块均可枚举 - `l1_ext_shell_list_commands_a_z`），并将其渲染为启动器：扩展接口面的参考消费者。

## 扩展接口面

`tui_ext.h` - 37 个可调用声明（窗口、菜单、表单、面板、文本）。L1 模块通过它构建 UI；CN 变体（`tui_cn_*.c`）展示了中文层在其下的模式。

## 渲染路径

帧缓冲几何信息来自 multiboot2 帧缓冲标签；控制台网格和 8x16 字体引擎在 IDT 之前初始化（启动日志顺序很重要 - TUI 使用内核横幅使用的相同原语进行绘制）。`tui-test` 是针对所有这些的客内回归测试套件。
