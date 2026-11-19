<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Display (13)

### chinese

| Field | Value |
|---|---|
| Name | chinese |
| Category | Display |
| Function | CJK console demo |
| Usage | `chinese` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
chinese
chinese support status:
  font     : embedded-unifont-16x16
  coverage : 6763 hanzi + 682 symbols, 16x16, 2 bytes/row
  data     : 216416 + 21824 bytes embedded
  slots    : 1 of 4 used
  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 5904 | encode 0 | convert 0 | detect 0 | miss 0
  console encoding: utf-8
ush> 
```

(Note: full-screen page; the quit key was not honored in this session - output as captured, session reset)

### chinesetest

| Field | Value |
|---|---|
| Name | chinesetest |
| Category | Display |
| Function | CJK font/encoding test battery |
| Usage | `chinesetest` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

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

(Note: full-screen page; the quit key was not honored in this session - output as captured, session reset)

### clear

| Field | Value |
|---|---|
| Name | clear |
| Category | Display |
| Function | clear the screen |
| Usage | `clear` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
clear
ush> 
```

### encoding

| Field | Value |
|---|---|
| Name | encoding |
| Category | Display |
| Function | console encoding status/set |
| Usage | `encoding` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
encoding
encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 93 | encode 0 | convert 0 | detect 0 | miss 0
ush> 
```

### encoding-test

| Field | Value |
|---|---|
| Name | encoding-test |
| Category | Display |
| Function | encoding codec test |
| Usage | `encoding-test` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
encoding-test
encoding-test: PASS (utf8+gb2312 spot checks)
ush> 
```

### font

| Field | Value |
|---|---|
| Name | font |
| Category | Display |
| Function | font engine status |
| Usage | `font` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | font-test |
| Category | Display |
| Function | font renderer test |
| Usage | `font-test` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
font-test
font-test: rendering sample row (16x16 CJK):

font-test: 16px row drawn above (zhongwen OpenCube shi)

ush> 
```

### ime

| Field | Value |
|---|---|
| Name | ime |
| Category | Display |
| Function | the pinyin IME (-i interactive) |
| Usage | `ime [-i]` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

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
  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 3016 | encode 0 | convert 0 | detect 0 | miss 0
ush> 
```

### ime-test

| Field | Value |
|---|---|
| Name | ime-test |
| Category | Display |
| Function | IME engine + session battery |
| Usage | `ime-test` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
ime-test
ime-test: PASS (engine ni/hao/qqq + session: phrase, compose, page, digit, space, enter, backspace, esc, passthrough)
ush> 
```

### lang

| Field | Value |
|---|---|
| Name | lang |
| Category | Display |
| Function | show/set the console language (en/zh) |
| Usage | `lang [en\|zh]` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
lang
Console output language: en
ush> 
```

### serial_encoding

| Field | Value |
|---|---|
| Name | serial_encoding |
| Category | Display |
| Function | show/set the serial line encoding (utf8/gbk) |
| Usage | `serial_encoding [utf8\|gbk] (WP06g3` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
serial_encoding
serial encoding: utf8
ush> 
```

### stats

| Field | Value |
|---|---|
| Name | stats |
| Category | Display |
| Function | display pipeline statistics |
| Usage | `stats` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
stats
ticks=291  ms=2910  IRQ0=292  IRQ1=0  soft=0  one=0
ush> 
```

### uname

| Field | Value |
|---|---|
| Name | uname |
| Category | Display |
| Function | system identification |
| Usage | `uname [-a\|-s\|-r\|-m]` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
uname
Open Cube OS
ush> 
```
