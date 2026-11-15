<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI 引擎：全屏桌面及其页面

TUI 家族是 `ocos/services/tui/`——25 个源文件，约 1.34 万行，是
仅次于 font 的第二大服务家族。它拥有 L1SVC 类别 20，共 **九个 op，
全部直通路由**（`out[0]=0`；实时控制台输出即会话，并逐字镜像到
串口分岔）：

```c
#define L1SVC_TUI_TUI        1   /* tui      (desktop)       */
#define L1SVC_TUI_TEST       2   /* tui-test (selftests)     */
#define L1SVC_TUI_HW         3   /* tui-hw   [page]          */
#define L1SVC_TUI_NET        4   /* tui-net                  */
#define L1SVC_TUI_SYSINFO    5   /* tui-sysinfo [page]       */
#define L1SVC_TUI_TIME       6   /* tui-time                 */
#define L1SVC_TUI_DRV        7   /* tui-drv   [page]         */
#define L1SVC_TUI_SYS        8   /* tui-sys   [page]         */
#define L1SVC_TUI_EXT        9   /* tui-ext                  */
```

一个桌面 + 八个页面：op 1 是全屏桌面；其余八个是经典全屏页面及其
自测面（`tui-drv` 保留其 v2.2.0 的别名角色——与 `tui-hw` 同体，
独立 op 编号）。

## 为什么每个 op 都直通运行（kcap 约束）

L1SVC 文本面通常经 `core_kcap_run_fn` 捕获命令体的控制台输出——
捕获激活期间，捕获钩子即控制台钩子，**帧缓冲与串口分岔都看不到
这些字符**（见 [hidden-mechanisms.md](hidden-mechanisms.md)）。这对
TUI 恰恰是错的：全屏会话是逐键实时渲染的，其输出必须在*会话运行
期间*抵达屏幕。以捕获方式路由 TUI op 会让整个会话期间屏幕一片
空白，然后再把 30 KiB 的 ANSI 杂烩重放进缓冲区。

裁决记录在源码里：`tui-test` 最初走捕获路由，但其结论块约 4.4
KiB——4096 字节的服务结果缓冲截断了最终摘要，于是像 v2.2.0 一样
改为直通。同一规则早已约束 `sshd` 与 `power sleep`（被捕获的多秒
驻留会吞掉其间打印的每一行控制台输出——OCIRE 监督引导期间实测
发现）。

## 文件清单

该家族 `src/` 目录，按角色：

| 组 | 文件 | 角色 |
|---|---|---|
| dispatch | `svc_tui_l1.c`、`svc_tui_cmd.{c,h}` | cat-20 路由（全直通）+ 命令体 |
| engine | `core`、`win`、`panel`、`menu`、`text`、`form`、`input` | 窗口栈、面板、菜单、文本控件、表单、输入泵 |
| desktop | `desktop`、`dt_apps`、`dt_cmdbr`、`dt_editor`、`dt_edtrm`、`dt_files`、`dt_proc` | 全屏桌面环境 |
| classic pages | `hw`、`net`、`sysinfo`、`time`、`sys`、`drv` | 分页的信息屏幕 |
| CJK | `chinese`、`cn_hw`、`cn_net`、`cn_sysinfo` | 同一批页面的中文渲染 |
| extension | `ext`、`exec` | 主题/热键注册表与应用执行桥 |
| tests | `test` | `tui-test` 测试组 |

`cn_*` 文件之所以存在，是因为每个经典页面都有一个中文孪生：
`tui-hw` 与 `tui-hw` 等价的 CJK 渲染共享数据抓取器，但经
`svc_font_render_cn` 以双语标签渲染
（见 [chinese-layer.md](chinese-layer.md)）。

## 桌面（`tui` op 1）

`svc_tui_desktop.c` + `dt_*` 文件：一个全屏桌面环境，含：

- **dt_apps**——应用启动器（把 L1 程序注册表渲染为可启动条目），
- **dt_cmdbr**——命令栏，
- **dt_editor / dt_edtrm**——内建编辑器与终端，
- **dt_files**——文件管理器（VFS 后备），
- **dt_proc**——进程查看器（`SYS_PS` 表）。

渲染经 CJK 感知渲染器（`svc_font_render_cn`）与窗口/面板/菜单/
文本/表单/控件层（`svc_tui_{win,panel,menu,text,form}.c`）；输入
来自控制台输入复用器（PS/2 + 串口 CSI 解码方向键；xterm SGR 鼠标
报告原样透传给鼠标解码器）。IME 即*系统* IME 引擎
（`svc_display_ime_engine.h`）——与编辑器所用为同一引擎
（见 [chinese-layer.md](chinese-layer.md)）。

## 经典页面

| 页面 | 内容 |
|---|---|
| `tui-hw` / `tui-drv` `[page]` | 硬件/驱动清单，分页——PCIe 缓存、块设备、网卡、声卡、USB 树 |
| `tui-net` | 网络状态——接口、ARP、路由、套接字、防火墙 |
| `tui-sysinfo` `[page]` | 系统信息——版本、运行时长、内存、任务 |
| `tui-time` | 时钟源——RTC/HPET/PM 定时器/TSC 状态 |
| `tui-sys` `[page]` | 系统管理——health、klog 尾部、看门狗 |
| `tui-ext` | TUI 扩展面演示（已注册的热键/主题——见 [../l1/tui-ext.md](../l1/tui-ext.md)） |
| `tui-test` | 自测组（窗口管理器、控件、输入、渲染检查） |

## 自测组（`tui-test` op 2）

`tui-test` 在屏幕上实时运行引擎自己的验收套件：窗口创建/销毁/
z 序、面板与菜单导航、文本控件渲染（含宽单元格 CJK）、表单字段
编辑、输入泵、脏矩形重绘正确性，以及扩展注册表。其结论块约
4.4 KiB——正是决定该家族全直通路由的史实（见上）。E2E 测试组
（`tools/tui_g11c_e2e.py` 与 tui 验收笔录）通过 grep 这份输出的
串口镜像来无头验证页面。

## 输入与输出管道

- **按键**：控制台输入复用器合并 PS/2 队列与 COM1 串口桥；桥上的
  ANSI CSI 解码器把 `ESC [ A/B/C/D` 方向键与 `ESC [ H/F` Home/End
  预解码为 OC_KEY_* 码，使串口终端与本地键盘同等地驱动 TUI
  （见 [interrupts.md](interrupts.md)）。
- **鼠标**：xterm SGR 报告（`ESC [ < ... M/m`）经 CSI 解码器原样
  透传，交给 TUI 自己的鼠标解码器；本地指针输入来自 USB HID 鼠标
  与 i2c-hid 触摸板（见 [driver-model.md](driver-model.md)）。
- **输出**：每个页面把 ANSI 转义序列渲染进控制台；串口分岔逐字
  镜像它们——这正是验收笔录可 grep、无头自动化得以工作的原因。

## 初始化与扩展点

该家族的 `OC_SERVICE_DECL_L1` 携带带入口点服务中唯一的 init 槽位：
`tui_ext_init()` 在**第一个 op 运行之前**预热内建主题与热键——
扩展注册表必须在任何页面请求主题之前存在。已注册的扩展（主题、
热键）跨 op 存活整个会话。

引擎自身的原语（窗口栈、z 序、脏矩形重绘、ANSI 渲染）是
`svc_tui_{core,win,panel}.c` 的内部实现；面向 L1 应用的公开扩展面
文档见 [../l1/tui-ext.md](../l1/tui-ext.md)，用户指南见
[../manual/tui-guide.md](../manual/tui-guide.md)。

## 与其余部分的关系

- **字体/编码**：每个页面经与控制台相同的 CJK 管道渲染——宽单元
  格用 16x16 GB2312 字形，否则 8x16 ASCII
  （见 [chinese-layer.md](chinese-layer.md)）。
- **数据源**：页面是渲染器，渲染的是 CLI 所读的同一批内核表——
  `net_get_stats()`、套接字表、`SYS_PS`、PMM/堆统计、klog 重放、
  看门狗槽位（见 [network-stack.md](network-stack.md)、
  [scheduler.md](scheduler.md)、[../internals/klog.md](../internals/klog.md)）。
- **串口镜像**：由于各 op 直通，串口分岔承载完整 ANSI 会话——
  E2E 测试组（`tools/tui_g11c_e2e.py`、tui 验收笔录）正是这样无头
  验证页面的。
- **配置开关已除名**：`CONFIG_TUI`/`CONFIG_CHINESE` 随旧 TUI 平面
  一并删除——CJK 管道与 TUI 无条件编译。
