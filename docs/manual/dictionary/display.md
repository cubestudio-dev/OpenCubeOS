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
```

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
```

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
  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 9084 | encode 0 | convert 0 | detect 0 | miss 0
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
| Usage | `lang [en|zh]` |
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
| Usage | `serial_encoding [utf8|gbk] (WP06g3` |
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
ticks=607  ms=6070  IRQ0=607  IRQ1=0  soft=0  one=0
ush> 
```

### uname

| Field | Value |
|---|---|
| Name | uname |
| Category | Display |
| Function | system identification |
| Usage | `uname [-a|-s|-r|-m]` |
| Source | `ocos/services/display/src/svc_display_l1.c` |

**Executed example**:

```
uname
Open Cube OS
ush> 
```
