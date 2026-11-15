<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI 服务表面

桌面住在 TUI 服务（`ocos/services/tui/`）里，并经 L1SVC "tui"
类别触达 ring 3（操作族 1-9：`tui`、`tui-test`、`tui-hw`、
`tui-net`、`tui-sysinfo`、`tui-time`、`tui-drv`、`tui-sys`、
`tui-ext`）。

## 扩展注册表（svc_tui_api.h）

所有表都是静态的（独立环境编译、无 malloc 依赖）；注册成功
返回 0，表满返回 -1，名字/键重复返回 -2。每个条目携带其所有者
（树内页为 "builtin-..."），因此注销可用且重复注册是幂等的。

| 注册表 | API | 新增什么 |
|----------|-----|--------------|
| menu | `tui_menu_register/unregister/list/sort` | 一个启动器项：label、desc、ASCII 图标、group、hotkey、priority、动作回调 |
| page | `tui_page_register/unregister/list/render/input` | 一个完整页面：render(win, scroll) + input(key) -> 1 已消费；`tui_ext_page_main` 是通用 runner |
| widget | `tui_widget_register/unregister/list/render/event` | 一种可复用部件类型 |
| command | `tui_cmd_register/unregister/list/execute` | 一个 TUI 命令行钩子 |
| theme | `tui_theme_register/unregister/list/set` | 颜色主题 |
| hotkey | `tui_key_register/unregister/list/conflict` | 全局热键（内置冲突检测） |
| event | `tui_event_register/unregister/list/dispatch` | 通用事件订阅者 |

限额是命名常量（`TUI_EXT_MENU_MAX`、...）- 有界表、响亮地
拒绝、绝不截断。

## 参考实现

树内页面正是这样注册的 - `svc_tui_time.c`（`time.clock`、
`time.tz`、`time.set`）、`svc_tui_sys.c`（`sys.host`、...）、
hw/net/sysinfo/drv/sys 各族，以及桌面应用（`svc_tui_dt_*.c`）。
写你自己的之前先读一个：init 时注册、teardown 时注销、带所有者
标签。

## 页面与程序

TUI 渲染的是合并应用包装的同一批服务 - sysinfo 页读的正是
`hwinfo` 读的那些 L1SVC 操作。TUI 页是内核内的服务表面（经上面
的注册表）；大应用是 ring-3 程序；两扇门通向同一状态。
