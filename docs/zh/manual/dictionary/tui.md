<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 全屏 TUI 家族 (9)

> 隐藏条目口径（WP-tui-input-encoding-hardening）：下列八个子启动器
> （`tui-test`、`tui-hw`、`tui-net`、`tui-sysinfo`、`tui-time`、
> `tui-drv`、`tui-sys`、`tui-ext`）保持注册、可直接执行，但已从
> `help`、`help -a`、`help -w` 与桌面菜单树隐藏。`tui` 本身保持列出
> —— 它是唯一的桌面入口。

### tui

| 字段 | 值 |
|---|---|
| 名称 | tui |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 桌面 |
| 用法 | `tui      (desktop` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-drv

| 字段 | 值 |
|---|---|
| 名称 | tui-drv |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 驱动页 |
| 用法 | `tui-drv   [page]` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-drv

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-ext

| 字段 | 值 |
|---|---|
| 名称 | tui-ext |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 扩展页 |
| 用法 | `tui-ext` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-ext

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-hw

| 字段 | 值 |
|---|---|
| 名称 | tui-hw |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 硬件页 |
| 用法 | `tui-hw   [page]` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-hw

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-net

| 字段 | 值 |
|---|---|
| 名称 | tui-net |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 网络页 |
| 用法 | `tui-net` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-net

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-sys

| 字段 | 值 |
|---|---|
| 名称 | tui-sys |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 系统页 |
| 用法 | `tui-sys   [page]` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-sys

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-sysinfo

| 字段 | 值 |
|---|---|
| 名称 | tui-sysinfo |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 系统信息页 |
| 用法 | `tui-sysinfo [page]` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-sysinfo

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))

### tui-test

| 字段 | 值 |
|---|---|
| 名称 | tui-test |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 自检 |
| 用法 | `tui-test (selftests` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-test
[PASS] term detect returns 0
    input    : tui_term_detect()
    expected : 0
    actual   : 0
[PASS] term size nonzero
    input    : cols*rows
    expected : 1
    actual   : 1
[PASS] term detected
    input    : tui.backend
    expected : opencube-fb|ansi-serial
    actual   : term=opencube-fb
[PASS] window alloc
    input    : new(0,0,10,40)
    expected : 1
    actual   : 1
[PASS] window move in-bounds
    input    : move(2,3)
    expected : 0
    actual   : 0
[PASS] window move rejects -x
    input    : move(0,-1)
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] window resize
    input    : resize(12,50)
    expected : 0
    actual   : 0
[PASS] print lands in cells
    input    : print_at(0,0,"AB")
    expected : 65
    actual   : 65
[PASS] second char cell
    input    : cells[1]
    expected : 66
    actual   : 66
[PASS] border corner
    input    : cells[0] after border
    expected : 9484
    actual   : 9484
[PASS] printw number
    input    : printw(x=42)
    expected : 120
    actual   : 120
[PASS] printw string tail
    input    : cells[8] == 'k'
    expected : 107
    actual   : 107
[PASS] keymap arrows
    input    : OC_KEY_UP
    expected : 257
    actual   : 257
[PASS] CSI arrow sequence
    input    : ESC [ B
    expected : 258
    actual   : 258
[PASS] SGR mouse press
    input    : ESC[<0;1;2M
    expected : 288
    actual   : 288
[PASS] mouse event queued
    input    : tui_mouse_get
    expected : 0
    actual   : 0
[PASS] mouse coords
    input    : y = p2-1 = 1
    expected : 1
    actual   : 1
[PASS] pair out of range
    input    : init_pair(999)
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] pair roundtrip
    input    : pair(2,1)
    expected : 18
    actual   : 18
[PASS] 256-color exact base
    input    : 256 idx 2 -> palette 2
    expected : 2
    actual   : 2
[PASS] panel create
    input    : panel_new
    expected : 1
    actual   : 1
[PASS] panel visible
    input    : panel_hidden
    expected : 0
    actual   : 0
[PASS] panel hides
    input    : hide->hidden
    expected : 1
    actual   : 1
[PASS] panel show again
    input    : show->not hidden
    expected : 0
    actual   : 0
[PASS] menu first item
    input    : current after post
    expected : 0
    actual   : 0
[PASS] menu nav down
    input    : driver(DOWN)
    expected : 1
    actual   : 1
[PASS] menu nav enter
    input    : driver(ENTER)
    expected : 1
    actual   : 1
[PASS] menu nav esc
    input    : driver(ESC)
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] form buffer set
    input    : set_field(0,"hello")
    expected : hello
    actual   : hello
[PASS] form edit append
    input    : type X, BS, type Y
    expected : helloY
    actual   : helloY
[PASS] mem stats live
    input    : total_pages>0
    expected : 1
    actual   : 1
[PASS] nic probe ran
    input    : num_devices>=0
    expected : 1
    actual   : 1
[PASS] block devices probed
    input    : count>=0
    expected : 1
    actual   : 1
[PASS] snd probe ran
    input    : num_devices>=0
    expected : 1
    actual   : 1
[PASS] pcie scan ran
    input    : count>=0
    expected : 1
    actual   : 1
[PASS] camera probe ran
    input    : count>=0
    expected : 1
    actual   : 1
[PASS] wireless probe ran
    input    : count>=0
    expected : 1
    actual   : 1
[PASS] config write/read
    input    : tui.selftest
    expected : 0
    actual   : 0
[PASS] config value roundtrip
    input    : value
    expected : wp10
    actual   : wp10
[PASS] wifi connect honest
    input    : unknown ssid -> -1
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] bt present honest
    input    : bt_hci_present>=0
    expected : 1
    actual   : 1
[PASS] task table live
    input    : tasks>=1
    expected : 1
    actual   : 1
[PASS] log ring live
    input    : ring lines>0
    expected : 1
    actual   : 1
[PASS] log ring readable
    input    : ring_get(0)
    expected : 0
    actual   : 0
[PASS] timer ticks advance
    input    : t2>t1
    expected : 1
    actual   : 1
[PASS] power: S3 capability honest (mask+slp_typ agree)
    input    : cap
    expected : 1
    actual   : 1
[PASS] power: sleep mask / SLP_TYP consistent
    input    : agree
    expected : 1
    actual   : 1
[PASS] getch delivers an injected key
    input    : inject 'x'
    expected : 120
    actual   : 120
[PASS] injected key is immediate
    input    : dt<100ms
    expected : 1
    actual   : 1
[PASS] empty queue times out with -1
    input    : 50ms budget
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] empty queue actually blocked
    input    : dt>=40ms
    expected : 1
    actual   : 1
[PASS] lone ESC delivers with next byte
    input    : ESC
    expected : 27
    actual   : 27
[PASS] stash flushes when queue drains
    input    : '2' at once
    expected : 50
    actual   : 50
[PASS] no duplicate delivery after flush
    input    : -1
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] window pool survives 40 new/free cycles
    input    : 40 cycles
    expected : 1
    actual   : 1
[PASS] pool occupancy after 40 cycles
    input    : tui_window_count
    expected : ?
    actual   : 1
[PASS] pool fills to 16 live windows
    input    : 16 allocated
    expected : 16
    actual   : 16
[PASS] 17th window refused honestly
    input    : pool full -> NULL
    expected : 1
    actual   : 1
[PASS] freed slot is reusable
    input    : new after free
    expected : 1
    actual   : 1
[PASS] Esc key maps through OC_KEY_ESC_KEY
    input    : map_key(0x9C)
    expected : 27
    actual   : 27
[PASS] double ESC delivers Esc
    input    : 0x1B 0x1B
    expected : 27
    actual   : 27
[PASS] desktop: root node add
    input    : t.l1
    expected : 0
    actual   : 0
[PASS] desktop: duplicate id refused
    input    : t.l1 again
    expected : 18446744073709551614
    actual   : 18446744073709551614
[PASS] desktop: unknown parent refused
    input    : parent=nope
    expected : 18446744073709551612
    actual   : 18446744073709551612
[PASS] desktop: level-6 node accepted
    input    : t.l1..t.l6
    expected : 0
    actual   : 0
[PASS] desktop: depth(t.l6) == 6
    input    : tui_dt_depth
    expected : 6
    actual   : 6
[PASS] desktop: level-7 refused
    input    : t.l7 under t.l6
    expected : 18446744073709551611
    actual   : 18446744073709551611
[PASS] desktop: remove subtree
    input    : remove t.l1
    expected : 6
    actual   : 6
[PASS] desktop: removed id not found
    input    : find t.l1
    expected : 0
    actual   : 0
[PASS] guard: NULL fn rejected
    input    : fn==0
    expected : 18446744073709551615
    actual   : 18446744073709551615
[PASS] guard: panic contained
    input    : panic -> -1000
    expected : 18446744073709550616
    actual   : 18446744073709550616
[PASS] guard: still usable after containment
    input    : ok fn
    expected : 0
    actual   : 0
[PASS] language: zh label + state
    input    : shell.lang=zh
    expected : 1
    actual   : 1
[PASS] language: back to en
    input    : shell.lang=en
    expected : 1
    actual   : 1
[PASS] files: mkdir /tui-unified-t
    input    : mkdir
    expected : 0
    actual   : 0
[PASS] files: create + write a.txt
    input    : 11 bytes
    expected : 0
    actual   : 0
[PASS] files: copy a.txt -> b.txt
    input    : 11 bytes copied
    expected : 11
    actual   : 11
[PASS] files: rename b.txt -> c.txt
    input    : rename
    expected : 0
    actual   : 0
[PASS] files: stat size preserved
    input    : size 11
    expected : 1
    actual   : 1
[PASS] files: delete c.txt
    input    : unlink
    expected : 0
    actual   : 0
[PASS] files: a.txt untouched
    input    : stat ok
    expected : 1
    actual   : 1
[PASS] files: readdir sees a.txt only
    input    : 1 entry
    expected : 1
    actual   : 1
[PASS] desktop: bootstrap fills registry
    input    : count>=30
    expected : 1
    actual   : 1
[PASS] desktop: files app kind
    input    : TUI_DT_APP
    expected : 2
    actual   : 2
[PASS] desktop: files app runner
    input    : run!=0
    expected : 1
    actual   : 1
[PASS] desktop: procs app kind
    input    : TUI_DT_APP
    expected : 2
    actual   : 2
[PASS] desktop: editor app kind
    input    : TUI_DT_APP
    expected : 2
    actual   : 2
[PASS] desktop: editor runner wired
    input    : tui_dt_editor_run
    expected : 1
    actual   : 1
[PASS] desktop: editor at root level
    input    : depth 1
    expected : 1
    actual   : 1
[PASS] desktop: terminal node kind
    input    : TUI_DT_TERMINAL
    expected : 4
    actual   : 4
[PASS] desktop: terminal runner wired
    input    : tui_dt_terminal_run
    expected : 1
    actual   : 1
[PASS] desktop: terminal at root level
    input    : depth 1
    expected : 1
    actual   : 1
[PASS] priv: containment payload embedded
    input    : ELF64
    expected : 1
    actual   : 1
tui-test: 93/93 checks passed, all green
ush> 
```

(备注: full-screen page; the quit key was not honored in this session - output as captured, session reset)

### tui-time

| 字段 | 值 |
|---|---|
| 名称 | tui-time |
| 分类 | 全屏 TUI 家族 |
| 功能 | TUI 时钟页 |
| 用法 | `tui-time` |
| 来源 | `ocos/services/tui/src/svc_tui_l1.c` |

**实际执行示例**：

```
tui-time

(returned to the ush prompt after Esc)
```

(备注: full-screen page; Esc exits (driven via the QEMU monitor sendkey))
