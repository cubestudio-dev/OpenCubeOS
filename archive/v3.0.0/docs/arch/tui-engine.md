<!-- SPDX-License-Identifier: Apache-2.0 -->
# The TUI engine

`tui/` is a windowing system on the framebuffer console:

## Core

`tui_core.c` (the event loop + window manager), `tui_win.c`
(windows), `tui_menu.c`, `tui_form.c`, `tui_panel.c`, `tui_text.c`
(widgets), `tui_input.c` (keyboard routing), `tui_hw.c` (render
diagnostics).

## Desktop

`tui_desktop.c` + `tui_dt_*`: taskbar, launcher, the apps (files,
editor, process/system monitor, network status, sysinfo, clock).

## The command bridge

`tui_cmd.c` enumerates the shell command table through the L1 API
(any L1 module may enumerate - `l1_ext_shell_list_commands_a_z`) and
renders it as the launcher: the reference consumer of the extension
surface.

## Extension surface

`tui_ext.h` - 37 callable declarations (windows, menus, forms,
panels, text). L1 modules build UI through it; the CN variants
(`tui_cn_*.c`) show the pattern with the Chinese layer under them.

## Rendering path

Framebuffer geometry comes from the multiboot2 framebuffer tag; the
console grid and the 8x16 font engine come up before the IDT does
(the boot log order matters - the TUI draws with the same primitives
the kernel banner used). `tui-test` is the in-guest regression suite
for all of it.
