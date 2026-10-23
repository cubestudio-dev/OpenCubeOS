<!-- SPDX-License-Identifier: Apache-2.0 -->
# 显示与帧缓冲

## 链路

multiboot2 帧缓冲标签（几何参数 + 跨度 + 掩码） ->
`screen_fb.c`（帧缓冲基础） -> `screen_font.c` +
`screen_font_data.c`（8x16 引擎） -> `screen_console.c`（控制台网格） ->
Shell 行编辑器与 TUI。

## L1 接口面

`l1_ext_fb_get_info()`（供模块渲染器使用的几何参数）和字体引擎协议
`l1_ext_register_font_engine`（中文 16x16 字体通过此协议注册）+
控制台钩子（`l1_ext_set_console_hook`），允许模块拦截输出。

## 命令与测试

`tui-hw`（渲染诊断）、`font`/`font-test`（引擎）、TUI 套件 `tui-test`。
在 QEMU 中，默认的 `-vga std` 提供 800x600x32 - 启动日志会打印其找到的确切几何参数和掩码。
