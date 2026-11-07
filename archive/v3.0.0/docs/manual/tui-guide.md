<!-- SPDX-License-Identifier: Apache-2.0 -->
# TUI guide (terminal user interface)

The TUI layer (`tui/`) is a windowing system on the framebuffer:
desktop, taskbar, menus, forms, panels, a text editor and system
apps - all keyboard-driven.

## Starting it

```
oc> tui
```

The desktop takes over the screen. (In QEMU, drop `-display none` and
use the SDL/ GTK window so you can see it; the serial console keeps
running alongside.)

## The desktop

- A taskbar with a launcher.
- Windows with the standard widget set: menus, forms, text areas,
  panels.
- The bundled apps cover the system monitor (processes, memory),
  files, a TUI editor, network status and system info.

## Keyboard model

- Arrow keys / Tab navigate; Enter activates; Esc backs out.
- The editor uses the same line discipline as the shell line editor.
- `tui` apps are reachable both from the desktop launcher and as
  individual commands - see the TUI domain in the
  [commands index](commands/index.md).

## TUI commands

```
oc> tui              (enter the desktop)
oc> tui-test         (the TUI regression suite)
oc> tui-hw           (TUI hardware/render diagnostics)
oc> tui-net          (network status app)
oc> tui-sysinfo      (system information app)
oc> tui-time         (clock/time sources panel)
oc> tui-sys          (system monitor)
oc> tui-drv          (driver inventory)
oc> tui-ext          (extension surface status)
```

## Chinese console and TUI

The Chinese layer (`chinese` command) swaps the console into a
16x16 CJK font with GB encoding and a pinyin IME, and provides CN
variants of the hardware/net/sysinfo TUI apps:

```
oc> chinese          (status of font/encoding/render/ime)
oc> ime -i           (interactive pinyin input line)
oc> lang             (language console switch)
```

System strings remain ASCII by contract; the Chinese layer renders
user content - see [chinese-console](chinese-console.md).

## Extension surface

L1 modules can create TUI windows and widgets through
`tui/tui_ext.h` (37 callable declarations - windows, menus, forms,
panels, text). The TUI command bridge itself is the reference
implementation: it enumerates the shell command table through the L1
API and renders it as a launcher. Extension guide:
[../l1/tui-ext.md](../l1/tui-ext.md).

## Building without the TUI

`make CONFIG_TUI=n` drops the TUI (and with `CONFIG_CHINESE=n` the
Chinese layer) from the image - the boot then registers 216 commands
(233 - 17). The off-path is real code with ASCII-equivalent routing,
not a deleted feature. See [../dev/build.md](../dev/build.md).
