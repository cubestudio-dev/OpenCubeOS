<!-- SPDX-License-Identifier: Apache-2.0 -->
# 中文控制台层

Open Cube OS 保持所有系统可见字符串为 ASCII，同时通过 `chinese/` 下的专用层渲染中文用户内容。

## 是什么

| 组件 | 文件 | 功能 |
|-------|------|------|
| 16x16 CJK 字体 | `chinese/chinese_font_data.c` | 控制台的位图字形 |
| GB 编码 | `chinese/encoding.c` | gb2312 / utf-8 / ascii 编解码器 |
| 中文控制台 | `chinese/console_cn.c` | 在帧缓冲上渲染 CJK 行 |
| 渲染器 | `chinese/render_cn.c` | 字形光栅管道 |
| 拼音输入法 | `chinese/pinyin_data.c` + ime | 音节解析与输入 |
| 中文 TUI 应用 | `tui_cn_*.c` | 中文界面的硬件/网络/系统信息 |

## 命令

```
oc> chinese          (status: font / encoding / render / ime)
oc> chinesetest      (full Chinese-support regression suite)
oc> font             (list registered fonts + active renderer)
oc> font-test        (render a mixed CJK/ASCII sample row)
oc> encoding         (codec status: gb2312 / utf-8 / ascii)
oc> encoding-test    (quick codec round-trip check)
oc> ime              (pinyin IME status)
oc> ime -i           (interactive pinyin input on the console line)
oc> lang             (language console switch)
```

## 设计规则

- **系统字符串为 ASCII**：命令名、提示符、启动消息、帮助文本——这是固件/硬件兼容性契约（参见 `CONTRIBUTING.md` 基本规则 1）。
- **用户内容可为任意**：文件名、文件内容、编辑器缓冲区、网络负载——中文层在控制台和 TUI 中渲染它们。

因此，没有 CJK 字体的终端仍能获得完全可用的系统；启用该层后，混合语言内容能正确显示。

## 构建配置

```
make CONFIG_CHINESE=n     (also forces CONFIG_TUI=n)
```

关闭路径是真实代码：`console_cn` 路由到 `screen_console` 中的 ASCII 等价物，配置器生成的头文件将禁用的调用映射到这些等价物。`CONFIG_CHINESE=n CONFIG_TUI=n` 构建启动时有 216 条命令（233 - 17 条中文/TUI 命令）——完整配置启动时有 233 条。
