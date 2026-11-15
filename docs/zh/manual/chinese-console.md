<!-- SPDX-License-Identifier: Apache-2.0 -->
# 中文控制台

## 切换

```
ush> lang zh        everything user-facing switches, live:
                    shell help, all 8 wizard apps, service surfaces
ush> lang en        back to English
ush> lang           show the current language
```

locale 按调用探测（不在首次渲染时缓存），因此在长时间运行的 ush 中执行 `lang zh` 会立即影响下一行帮助输出——包括在发起切换的那个 shell 里。

## 哪些内容被翻译

| 界面 | 机制 |
|---------|-----------|
| ush 内置命令 | 按应用的 i18n 表（`tr_()`） |
| 8 个大应用 | 按应用的 i18n 表——菜单、提示、判定 |
| 约 280 个经典应用 | 按应用的 i18n 表（`_hint` + 正文串） |
| L0 服务行（help -w 组名、注册表标题） | l0 表（`etc/i18n/l0/{en,zh}.json`） |

缺失的翻译会回退到英文，再回退到键本身——未翻译的应用绝不会崩溃或打印乱码；它显示键本身，让缺口可见、可修复。

## 渲染

CJK 管线始终编译在内：16x16 GB2312 点阵字体、GBK/UTF-8 解码链，以及控制台渲染器。串口和帧缓冲控制台都能渲染中文；串口编码遵循 `serial_encoding`（默认 `utf8`，可切换为 `gbk`）。

## 输入——输入法（IME）

输入法把拼音组合为 GB2312 的 16-55 区（一级字库：3755 个常用汉字），并提供词组补全：

```
ush> ime             (IME status: level-1 chars, phrases)
ush> ime -i          (interactive compose session)
```

组合会话内：输入拼音，选择候选，ESC 取消。显示服务通过 L1SVC 把同一引擎暴露给程序（IME begin / key / end 操作）。

## ASCII 契约

程序*名称*、文件路径和服务级的**输出行是 ASCII 英文**——它们是面向脚本的契约（向导和测试会探测这些行：`"A/B disk: present"`、`"New version available."`）。中文是面向*人*的呈现；脚本和探测在每个 locale 下都保持稳定的英文界面。
