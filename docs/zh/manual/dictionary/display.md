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
chinese support status:
  font     : embedded-unifont-16x16
  coverage : 6763 hanzi + 682 symbols, 16x16, 2 bytes/row
  data     : 216416 + 21824 bytes embedded
  slots    : 1 of 4 used
  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 92 | encode 0 | convert 0 | detect 0 | miss 0
  console encoding: utf-8
ush> 
```

(备注: full-screen page; the quit key was not honored in this session - output as captured, session reset)

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
chinesetest: full Chinese support suite
[1] font data
  PASS status() returns 0
  PASS hanzi count == 6763
  PASS symbol count == 682
  PASS glyph bytes == 32 (16x16, 2 B/row)
  PASS hanzi size == 216416 B
  PASS symbol size == 21824 B
  PASS embedded slot loaded
  PASS zone 16 wei 1 -> compact index 0
  PASS zone 87 wei 94 -> compact index 6762
  PASS zone 55 wei 90 is a GB2312 gap slot
  PASS all 6763 hanzi bitmaps non-empty
  PASS symbol table reachable via bit15
[2] font lookup
  PASS U+4E2D found in unicode index
  PASS zone/wei lookup agrees with unicode lookup
  PASS index -> unicode round-trips to U+4E2D
  PASS U+3002 in symbol table (bit15 set)
  PASS unmapped/mapped U+4E00 handled without crash
  PASS zone 95 rejected
  PASS wei 0 rejected
  PASS metrics 16x16 stride 2
[3] font management
  PASS embedded load idempotent
  PASS runtime slot allocated (>=1)
  PASS runtime font resolves its glyph
  PASS runtime unload ok
  PASS embedded slot unload rejected
  PASS unknown slot rejected
  PASS shutdown ok
[4] GB2312 codec
  PASS decode D6D0 -> U+4E2D
  PASS decode B0A2 -> U+963F (a, >= U+8000)
  PASS decode 'A' -> 0x41
  PASS lead 0x88 rejected
  PASS truncated pair rejected
  PASS encode U+4E2D -> D6 D0
  PASS encode U+963F -> B0 A2 (>= U+8000)
  PASS encode non-GB2312 cp rejected
[5] UTF-8 codec
  PASS decode E4 B8 AD -> U+4E2D
  PASS decode 'z' pass-through
  PASS overlong C1 81 rejected
  PASS surrogate ED A0 80 rejected
  PASS truncated 3-byte rejected
  PASS bad continuation byte rejected
  PASS decode 4-byte U+1F600
  PASS encode U+4E2D -> E4 B8 AD
  PASS encode surrogate rejected
  PASS encode > U+10FFFF rejected
[6] encoding conversion
  PASS GB2312 zhongwen -> UTF-8
  PASS UTF-8 -> GB2312 round trip
  PASS UTF-8 -> Unicode cp U+4E2D
[7] encoding detection
  PASS detect ascii
  PASS detect utf-8
  PASS detect gb2312
  PASS detect empty -> ascii
[8] renderer extension
  PASS render_chinese_init
  PASS CJK width 16 px
  PASS ASCII width 8 px
  PASS CJK cols 2
  PASS ASCII cols 1
  PASS font_select finds CJK font
  PASS font_get_bitmap CJK
  PASS ascii + gb2312 fonts registered
  PASS active renderer keeps 8x16 cell
  PASS draw CJK/ASCII/missing-box without fault
[9] console extension
  PASS console_cn_init
  PASS default encoding utf-8
  PASS encoding switch
  PASS mixed rejected
  PASS cp cols CJK 2
  PASS display cols a[zhong]b = 4
ä¸­x  PASS putcw cursor geometry SKIP (output captured)
   PASS cn backspace ok
[12] font engine registry
  PASS font_register accepts
  PASS count increments
  PASS registered font resolves cp
  PASS font_unregister
  PASS count restored
  PASS unregistered cp unresolved
  PASS unknown name rejected
[10] VFS UTF-8 file
  PASS create /tmp/cn.txt
  PASS wrote 12 UTF-8 bytes
[13] GB2312 full-library round trip
  PASS 6763 hanzi zone/wei <-> cp round trip
  PASS 682 symbols zone/wei <-> cp round trip
  full trip: 6763 hanzi (2366 at >= U+8000) + 682 symbols
chinesetest: 81/81 PASS
chinesetest: ALL PASS
ush> 
```

(备注: full-screen page; the quit key was not honored in this session - output as captured, session reset)

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
ush> 
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
encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 93 | encode 0 | convert 0 | detect 0 | miss 0
ush> 
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
encoding-test: PASS (utf8+gb2312 spot checks)
ush> 
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
fdref_test
FDREF PASS
[user] process 13 exited (code=0)
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
files nope
files: unknown program: nope
usage: files                (interactive menu)
       files direct <prog> [args]    (explicit passthrough)
       files <prog> [args]           (classic program dispatch)
programs:  ls
  tree
  df
  du
  cp
  mv
  rm
  edit
  stat
  chmod
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
ifconfig
no NIC detected
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
igb
igb: not present
ush> 
```

### lang

| 字段 | 值 |
|---|---|
| 名称 | lang |
| 分类 | 显示 |
| 功能 | 显示/设置控制台语言（en/zh） |
| 用法 | `lang [en\|zh]` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
ixgbe_test
[ixgbe_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### serial_encoding

| 字段 | 值 |
|---|---|
| 名称 | serial_encoding |
| 分类 | 显示 |
| 功能 | 显示/设置串口输出编码（utf8/gbk） |
| 用法 | `serial_encoding [utf8\|gbk] (WP06g3` |
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
ticks=290  ms=2900  IRQ0=290  IRQ1=0  soft=0  one=0
ush> 
```

### uname

| 字段 | 值 |
|---|---|
| 名称 | uname |
| 分类 | 显示 |
| 功能 | 系统标识 |
| 用法 | `uname [-a\|-s\|-r\|-m]` |
| 来源 | `ocos/services/display/src/svc_display_l1.c` |

**实际执行示例**：

```
uname
Open Cube OS
ush> 
```
