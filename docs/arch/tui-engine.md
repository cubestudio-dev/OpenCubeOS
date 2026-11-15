<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI engine: the full-screen desktop and its pages

The TUI family is `ocos/services/tui/` - 25 source files, ~13.4k lines,
the largest service family after font. It owns L1SVC category 20 with
**nine ops, all routed DIRECT** (`out[0]=0`; the live console output IS
the session, mirrored verbatim to the serial tee):

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

One desktop + eight pages: op 1 is the full-screen desktop; the other
eight are the classic full-screen pages and their self-test surface
(`tui-drv` keeps its v2.2.0 alias role - same body as `tui-hw`, own op
number).

## Why every op runs DIRECT (the kcap constraint)

The L1SVC text surface normally works by capturing a command body's
console output through `core_kcap_run_fn` - while a capture is active,
the capture hook IS the console hook, and **neither the framebuffer
nor the serial tee sees the characters**
([hidden-mechanisms.md](hidden-mechanisms.md)). That is precisely wrong
for a TUI: a full-screen session renders live, keystroke by
keystroke, and its output must reach the screen *while the session
runs*. Routing a TUI op captured would blank the screen for the whole
session and then replay 30 KiB of ANSI soup into a buffer.

The verdict is recorded in source: `tui-test` was first routed through
the capture, but its verdict block is ~4.4 KiB - the 4096-byte service
result buffer truncated the final summary, so it went direct like
v2.2.0. The same rule already governed `sshd` and `power sleep` (a
captured multi-second park swallowed every console line printed
meanwhile - found live during an OCIRE-supervised boot).

## The file inventory

The family's `src/` directory, by role:

| Group | Files | Role |
|---|---|---|
| dispatch | `svc_tui_l1.c`, `svc_tui_cmd.{c,h}` | cat-20 routing (all-direct) + command bodies |
| engine | `core`, `win`, `panel`, `menu`, `text`, `form`, `input` | the window stack, panels, menus, text widgets, forms, input pump |
| desktop | `desktop`, `dt_apps`, `dt_cmdbr`, `dt_editor`, `dt_edtrm`, `dt_files`, `dt_proc` | the full-screen desktop environment |
| classic pages | `hw`, `net`, `sysinfo`, `time`, `sys`, `drv` | the paged information screens |
| CJK | `chinese`, `cn_hw`, `cn_net`, `cn_sysinfo` | the Chinese renderings of the same pages |
| extension | `ext`, `exec` | the theme/hotkey registry and the app-execute bridge |
| tests | `test` | the `tui-test` battery |

The `cn_*` files exist because each classic page has a Chinese twin:
`tui-hw` and `tui-hw`-equivalent CJK rendering share the data
fetchers but render through `svc_font_render_cn` with bilingual labels
([chinese-layer.md](chinese-layer.md)).

## The desktop (`tui` op 1)

`svc_tui_desktop.c` + the `dt_*` files: a full-screen desktop
environment with:

- **dt_apps** - the application launcher (the L1 program registry
  rendered as launchable entries),
- **dt_cmdbr** - the command bar,
- **dt_editor / dt_edtrm** - the built-in editor and terminal,
- **dt_files** - the file manager (VFS-backed),
- **dt_proc** - the process viewer (the `SYS_PS` table).

Rendering goes through the CJK-aware renderer (`svc_font_render_cn`)
and the window/panel/menu/text/form/widget layer (`svc_tui_{win,panel, menu,text,form}.c`); input comes through the console input mux (PS/2 +
serial CSI-decoded arrows; xterm SGR mouse reports pass through raw
for the mouse decoder). The IME is the *system* IME engine
(`svc_display_ime_engine.h`) - the same engine the editors use
([chinese-layer.md](chinese-layer.md)).

## The classic pages

| Page | Content |
|---|---|
| `tui-hw` / `tui-drv` `[page]` | hardware/driver inventory, paged - the PCIe cache, block devices, NICs, sound cards, USB tree |
| `tui-net` | network status - interfaces, ARP, routes, sockets, firewall |
| `tui-sysinfo` `[page]` | system info - version, uptime, memory, tasks |
| `tui-time` | clock sources - RTC/HPET/PM-timer/TSC status |
| `tui-sys` `[page]` | system management - health, klog tail, watchdog |
| `tui-ext` | the TUI extension surface demo (registered hotkeys/themes - [../l1/tui-ext.md](../l1/tui-ext.md)) |
| `tui-test` | the self-test battery (window manager, widget, input, render checks) |

## The self-test battery (`tui-test` op 2)

`tui-test` runs the engine's own acceptance suite live on screen:
window create/destroy/z-order, panel and menu navigation, text
widget rendering (including wide-cell CJK), form field editing, the
input pump, dirty-rect repaint correctness, and the extension
registry. Its verdict block is ~4.4 KiB - the historical fact that
decided the family's all-direct routing (below). The E2E batteries
(`tools/tui_g11c_e2e.py` and the tui acceptance transcripts) verify
pages headlessly by grepping the serial mirror of exactly this
output.

## Input and output plumbing

- **Keys**: the console input mux merges the PS/2 queue and the COM1
  serial bridge; the bridge's ANSI CSI decoder pre-decodes
  `ESC [ A/B/C/D` arrows and `ESC [ H/F` Home/End into OC_KEY_* codes
  so a serial terminal drives the TUI identically to the local
  keyboard ([interrupts.md](interrupts.md)).
- **Mouse**: xterm SGR reports (`ESC [ < ... M/m`) pass through the
  CSI decoder raw, for the TUI's own mouse decoder; locally, pointer
  input comes from USB HID mice and the i2c-hid touchpad
  ([driver-model.md](driver-model.md)).
- **Output**: every page renders ANSI escape sequences into the
  console; the serial tee mirrors them verbatim - which is why the
  acceptance transcripts are greppable and why headless automation
  works at all.

## Init and extension points

The family's `OC_SERVICE_DECL_L1` carries the one init slot among
services with entry points: `tui_ext_init()` warms the builtin themes
and hotkeys **before the first op runs** - the extension registry must
exist before any page asks for a theme. Registered extensions
(themes, hotkeys) survive across ops for the whole session.

The engine's own primitives (window stack, z-order, dirty-rect
repaint, ANSI rendering) are internal to `svc_tui_{core,win,panel}.c`;
the public extension surface for L1 apps is documented in
[../l1/tui-ext.md](../l1/tui-ext.md), and the user guide is
[../manual/tui-guide.md](../manual/tui-guide.md).

## Relationship to the rest

- **Font/encoding**: every page renders through the same CJK pipeline
  as the console - 16x16 GB2312 glyphs for wide cells, 8x16 ASCII
  otherwise ([chinese-layer.md](chinese-layer.md)).
- **Data sources**: pages are renderers over the same kernel tables
  the CLI reads - `net_get_stats()`, the socket table, `SYS_PS`,
  PMM/heap stats, klog replay, watchdog slots
  ([network-stack.md](network-stack.md), [scheduler.md](scheduler.md),
  [../internals/klog.md](../internals/klog.md)).
- **Serial mirroring**: because the ops are direct, the serial tee
  carries the full ANSI session - which is exactly how the E2E
  batteries (`tools/tui_g11c_e2e.py`, the tui acceptance transcripts)
  verify pages headlessly.
- **The config knobs are gone**: `CONFIG_TUI`/`CONFIG_CHINESE` were
  deleted with the old TUI plane - the CJK pipeline and the TUI are
  unconditionally compiled in.
