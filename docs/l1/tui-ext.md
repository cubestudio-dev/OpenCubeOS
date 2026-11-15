<!-- SPDX-License-Identifier: Apache-2.0 -->
# The TUI service surface

The desktop lives in the TUI service (`ocos/services/tui/`) and
reaches ring 3 through the L1SVC "tui" category (op family 1-9:
`tui`, `tui-test`, `tui-hw`, `tui-net`, `tui-sysinfo`, `tui-time`,
`tui-drv`, `tui-sys`, `tui-ext`).

## The extension registries (svc_tui_api.h)

All tables are static (freestanding, no malloc dependency);
registration returns 0 on success, -1 table full, -2 duplicate
name/key. Every entry carries its owner ("builtin-..." for the
in-tree pages) so unregister works and re-registration is
idempotent.

| Registry | API | What it adds |
|----------|-----|--------------|
| menu | `tui_menu_register/unregister/list/sort` | a launcher entry: label, desc, ASCII icon, group, hotkey, priority, action callback |
| page | `tui_page_register/unregister/list/render/input` | a full page: render(win, scroll) + input(key) -> 1 consumed; `tui_ext_page_main` is the generic runner |
| widget | `tui_widget_register/unregister/list/render/event` | a reusable widget type |
| command | `tui_cmd_register/unregister/list/execute` | a TUI command-line hook |
| theme | `tui_theme_register/unregister/list/set` | colour themes |
| hotkey | `tui_key_register/unregister/list/conflict` | global hotkeys (conflict detection built in) |
| event | `tui_event_register/unregister/list/dispatch` | generic event subscribers |

Limits are named constants (`TUI_EXT_MENU_MAX`, ...) - bounded
tables, refused loudly, never truncated.

## The reference implementations

The in-tree pages register exactly this way - `svc_tui_time.c`
(`time.clock`, `time.tz`, `time.set`), `svc_tui_sys.c`
(`sys.host`, ...), the hw/net/sysinfo/drv/sys families, and the
desktop apps (`svc_tui_dt_*.c`). Read one before writing yours:
register on init, unregister on teardown, owner-tagged.

## Pages vs programs

The TUI renders the same services the merged apps wrap - the
sysinfo pages read the same L1SVC ops `hwinfo` does. TUI pages are
in-kernel service surfaces (through the registries above); the
mega-apps are ring-3 programs; both doors lead to the same state.
