<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: tui_ext.h - building UI from modules

`tui/tui_ext.h` is the TUI extension surface - 37 declarations in
seven registries. All tables are static (freestanding kernel, no
malloc dependency); registration returns 0 on success, -1 table
full, -2 duplicate name/key. Every entry carries its owner
("builtin" for in-kernel pages) so unregister works.

## The seven registries

| Registry | API | What it adds |
|----------|-----|--------------|
| 7.1 menu | `tui_menu_register/unregister/list/sort` | a launcher entry: label, desc, ASCII icon `[T]`, group, hotkey, priority, action callback |
| 7.2 page | `tui_page_register/unregister/list/render/input` + `tui_ext_page_main` | a full page: render(win, scroll) + input(key) -> 1 consumed; `tui_ext_page_main` is the generic runner |
| 7.3 widget | `tui_widget_register/unregister/list/render/event` | a reusable widget type |
| 7.4 command | `tui_cmd_register/unregister/list/execute` | a TUI command line hook; `tui_cmd_execute` returns fn's rc or -3 no-cmd |
| 7.5 theme | `tui_theme_register/unregister/list/set` | colour themes |
| 7.6 hotkey | `tui_key_register/unregister/list/conflict` | global hotkeys (conflict detection built in) |
| 7.7 event | `tui_event_register/unregister/list/dispatch` | generic event subscribers |

Limits are named constants (`TUI_EXT_MENU_MAX 24`,
`TUI_EXT_NAME_MAX 40`, ...) - bounded tables, refused loudly, never
truncated.

## The pattern that works

Register on module init; unregister on teardown; the owner tag makes
re-registration idempotent across module reloads. The CN TUI apps
(`tui_cn_hw/net/sysinfo`) and the TUI command bridge are the two
in-tree reference implementations - read one before writing yours.

## Rendering

Pages render through the same framebuffer primitives as the desktop
(`tui_win` windows); `tui_page_render` receives the window and a
scroll offset. Input: return 1 when your page consumed the key, 0 to
let it bubble.
