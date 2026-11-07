<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI 指南（终端用户界面）

TUI 层（`tui/`）是一个运行在帧缓冲上的窗口系统：
桌面、任务栏、菜单、表单、面板、文本编辑器和系统应用
全部通过键盘驱动。

## 启动它

```
oc> tui
```

桌面会接管整个屏幕。（在 QEMU 中，去掉 `-display none` 参数，
使用 SDL/GTK 窗口以便查看；串口控制台会继续在后台运行。）

## 桌面

- 带有启动器的任务栏。
- 包含标准组件集的窗口：菜单、表单、文本区域、面板。
- 捆绑的应用包括系统监控器（进程、内存）、
  文件、一个 TUI 编辑器、网络状态和系统信息。

## 键盘模型

- 方向键 / Tab 键导航；Enter 激活；Esc 返回。
- 编辑器使用与 Shell 行编辑器相同的行规。
- `tui` 应用既可通过桌面启动器访问，也可作为
  单独命令运行 - 参见命令索引中的
  [TUI 域](commands/index.md)。

## TUI 命令

```
oc> tui              (enter the desktop)
oc> tui-test         (the TUI regression suite)
oc> tui-hw           (TUI hardware/render diagnostics)
oc> tui-net          (network status app)
oc> tui-sysinfo      (system information app)
oc> tui-time         (clock/time sources panel)
oc> tui-sys          (system monitor)
oc> tui-drv          (driver inventory)
oc> tui-ext          (extension surface status)
```

## 中文控制台与 TUI

中文层（`chinese` 命令）将控制台切换为
16x16 CJK 字体并采用 GB 编码和拼音输入法，
同时提供硬件/网络/系统信息 TUI 应用的中文版本：

```
oc> chinese          (status of font/encoding/render/ime)
oc> ime -i           (interactive pinyin input line)
oc> lang             (language console switch)
```

系统字符串按约定保持 ASCII；中文层渲染用户内容 -
参见 [chinese-console](chinese-console.md)。

## 扩展接口面

L1 模块可通过 `tui/tui_ext.h`（37 个可调用声明 - 窗口、菜单、表单、面板、文本）
创建 TUI 窗口和组件。TUI 命令桥本身就是参考实现：
它通过 L1 API 遍历 Shell 命令表并将其渲染为启动器。扩展指南：
[../l1/tui-ext.md](../l1/tui-ext.md)。

## 不构建 TUI 进行构建

`make CONFIG_TUI=n` 会从镜像中移除 TUI（同时 `CONFIG_CHINESE=n` 会移除中文层），
启动后注册 216 个命令（233 - 17）。这个非完整路径是具有 ASCII 等价路由的真实代码，
而非被删除的功能。参见 [../dev/build.md](../dev/build.md)。
