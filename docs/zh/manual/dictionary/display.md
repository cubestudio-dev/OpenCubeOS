<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 显示 (13)

### chinese

| 字段 | 值 |
|---|---|
| 名称 | chinese |
| 分类 | 显示 |
| 功能 | 中文控制台演示 |
| 用法 | `chinese` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
chinese
```

### chinesetest

| 字段 | 值 |
|---|---|
| 名称 | chinesetest |
| 分类 | 显示 |
| 功能 | 中文字体/编码测试组 |
| 用法 | `chinesetest` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
chinesetest
```

### clear

| 字段 | 值 |
|---|---|
| 名称 | clear |
| 分类 | 显示 |
| 功能 | 清屏 |
| 用法 | `clear` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
clear
```

### encoding

| 字段 | 值 |
|---|---|
| 名称 | encoding |
| 分类 | 显示 |
| 功能 | 控制台编码状态/设置 |
| 用法 | `encoding` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
encoding
```

### encoding-test

| 字段 | 值 |
|---|---|
| 名称 | encoding-test |
| 分类 | 显示 |
| 功能 | 编码编解码测试 |
| 用法 | `encoding-test` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
encoding-test
```

### font

| 字段 | 值 |
|---|---|
| 名称 | font |
| 分类 | 显示 |
| 功能 | 字体引擎状态 |
| 用法 | `font` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
font
registered fonts:
  ascii-8x16     built-in screen_font8x16, 8x16
  gb2312-16x16   embedded GB2312, 6763+682 glyphs
  total live: 2
  active renderer: cn-8x16-16x16
ush> 
```

### font-test

| 字段 | 值 |
|---|---|
| 名称 | font-test |
| 分类 | 显示 |
| 功能 | 字体渲染测试 |
| 用法 | `font-test` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
font-test
font-test: rendering sample row (16x16 CJK):

font-test: 16px row drawn above (zhongwen OpenCube shi)

ush> 
```

### ime

| 字段 | 值 |
|---|---|
| 名称 | ime |
| 分类 | 显示 |
| 功能 | 拼音输入法（-i 交互） |
| 用法 | `ime [-i]` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
ime
pinyin IME status:
  syllables : 396
  level-1   : 3755 chars (GB2312 zones 16-55)
  phrases   : 622
  usage     : "ime -i" opens the interactive compose line (the SAME
              engine the nano/vi editors use - F2 inside an editor);
              type pinyin; digits 1-9 pick; space = first; PageUp/PageDown
              (or - / =) page; Backspace drops a letter; Enter commits the
  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 9084 | encode 0 | convert 0 | detect 0 | miss 0
ush> 
```

### ime-test

| 字段 | 值 |
|---|---|
| 名称 | ime-test |
| 分类 | 显示 |
| 功能 | 输入法引擎与会话测试组 |
| 用法 | `ime-test` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
ime-test
ime-test: PASS (engine ni/hao/qqq + session: phrase, compose, page, digit, space, enter, backspace, esc, passthrough)
ush> 
```

### lang

| 字段 | 值 |
|---|---|
| 名称 | lang |
| 分类 | 显示 |
| 功能 | 显示/设置控制台语言（en/zh） |
| 用法 | `lang [en|zh]` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
lang
Console output language: en
ush> 
```

### serial_encoding

| 字段 | 值 |
|---|---|
| 名称 | serial_encoding |
| 分类 | 显示 |
| 功能 | 显示/设置串口输出编码（utf8/gbk） |
| 用法 | `serial_encoding [utf8|gbk] (WP06g3` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
serial_encoding
serial encoding: utf8
ush> 
```

### stats

| 字段 | 值 |
|---|---|
| 名称 | stats |
| 分类 | 显示 |
| 功能 | 显示管线统计 |
| 用法 | `stats` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
stats
ticks=607  ms=6070  IRQ0=607  IRQ1=0  soft=0  one=0
ush> 
```

### uname

| 字段 | 值 |
|---|---|
| 名称 | uname |
| 分类 | 显示 |
| 功能 | 系统标识 |
| 用法 | `uname [-a|-s|-r|-m]` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
uname
Open Cube OS
ush> 
```
