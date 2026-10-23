<!-- SPDX-License-Identifier: Apache-2.0 -->
# 中文控制台层

`chinese/` 渲染 CJK 用户内容，而系统自身的字符串保持 ASCII：

## 组件

- `chinese_font_data.c` - 16x16 位图字形
- `encoding.c` - gb2312 / utf-8 / ascii 编解码器（已测试往返转换）
- `console_cn.c` - 帧缓冲上的 CJK 控制台行
- `render_cn.c` - 字形光栅流水线
- `pinyin_data.c` + 输入法 - 音节解析、交互式行
- `tui_cn_*.c` - 中文硬件/网络/系统信息 TUI 应用

## 双系统并存原理

ASCII 契约针对的是*系统*字符串（提示符、命令名、帮助、启动消息）- 硬件终端兼容性。当该层激活时，用户内容（文件名、文件体、编辑器缓冲区）通过编解码器流转，并在 CJK 字体上渲染。若设置 `CONFIG_CHINESE=n`，控制台将路由至 `screen_console` 中的 ASCII 等效实现 - 真实代码，两条路径，一个契约。

## 测试

`chinesetest`（完整回归测试）、`encoding-test`（编解码器往返转换）、`font-test`（混合行渲染）、`ime-test`（音节解析）。
