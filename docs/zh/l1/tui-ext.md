<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：tui_ext.h——用模块搭建界面

`tui/tui_ext.h` 是 TUI 扩展接口面——包含七个注册表中的 37 个声明。所有表都是静态的（独立的内核，无 malloc 依赖）；注册成功返回 0，表满返回 -1，重复名称/键返回 -2。每个条目都携带其所有者（内核内置页面为 "builtin"），因此注销功能可用。

## 七个注册表

| 注册表 | API | 功能 |
|--------|-----|------|
| 7.1 菜单 | `tui_menu_register/unregister/list/sort` | 启动器条目：标签、描述、ASCII 图标 `[T]`、组、热键、优先级、动作回调 |
| 7.2 页面 | `tui_page_register/unregister/list/render/input` + `tui_ext_page_main` | 完整页面：render(win, scroll) + input(key) -> 1 已消耗；`tui_ext_page_main` 是通用运行器 |
| 7.3 组件 | `tui_widget_register/unregister/list/render/event` | 可重用组件类型 |
| 7.4 命令 | `tui_cmd_register/unregister/list/execute` | TUI 命令行钩子；`tui_cmd_execute` 返回函数的返回码或 -3 无命令 |
| 7.5 主题 | `tui_theme_register/unregister/list/set` | 颜色主题 |
| 7.6 热键 | `tui_key_register/unregister/list/conflict` | 全局热键（内置冲突检测） |
| 7.7 事件 | `tui_event_register/unregister/list/dispatch` | 通用事件订阅者 |

限制是命名常量（`TUI_EXT_MENU_MAX 24`、`TUI_EXT_NAME_MAX 40` 等）——有界表，拒绝时明确提示，永不截断。

## 有效模式

在模块初始化时注册；在拆除时注销；所有者标签使跨模块重注册具有幂等性。中文 TUI 应用（`tui_cn_hw/net/sysinfo`）和 TUI 命令桥是树内两个参考实现——编写自己的前先阅读其中一个。

## 渲染

页面通过与桌面（`tui_win` 窗口）相同的帧缓冲基元进行渲染；`tui_page_render` 接收窗口和滚动偏移量。输入：页面消耗按键时返回 1，否则返回 0 以便继续传递。
