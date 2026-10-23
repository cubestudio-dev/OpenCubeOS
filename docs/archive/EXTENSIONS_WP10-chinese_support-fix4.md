# WP-10-chinese_support-fix4 - Extension interfaces

Batch: WP-10-chinese_support-fix4 (fourth fix batch over the Chinese
support work package)
Author: cubestudio-dev <cubestudio@qq.com>
License: Apache-2.0

This batch ships three things:

1. line-editor and rendering fixes (see README/STATUS),
2. the TUI as a second frontend for the system (time, drivers,
   system settings - parity with the shell through the same
   lib_config keys),
3. the TUI L1 extension registry: the same "everything is
   extensible" contract the shell has, now for the TUI.

Every interface below has its declaration in `tui/tui_ext.h`, its
implementation in `tui/tui_ext.c`, and a self-test exercised by
`tui-ext` (press `r` on the lab page, or read the lab page's
"last selftest" field).

## Command surface added by this batch

| Command    | Purpose                                                    |
|------------|------------------------------------------------------------|
| tui-time   | time TUI: clock & sources / timezone / set the RTC clock   |
| tui-drv    | driver setup TUI (alias of tui-hw, 12 hardware pages)      |
| tui-sys    | system settings TUI (8 pages, see below)                   |
| tui-ext    | TUI L1 extension lab: live registries + selftest           |

Command count: 221 -> 225 (nothing removed; `help` three modes
unchanged: default A-Z, `-w` by work package, `-a` all).

## 7.1 Menu extension

Extend the main TUI menu (`tui`) without touching tui_cmd.c. The
main menu lists the four builtin applications plus every registered
entry, sorted by (priority, name).

```c
int  tui_menu_register(const char *owner, const char *name,
                       const char *desc, const char *icon,
                       const char *group, int hotkey, int priority,
                       int (*action)(void), void *userptr);
int  tui_menu_unregister(const char *name);
int  tui_menu_list(tui_menuext_item_t *out, int cap);   /* -> count */
int  tui_menu_sort(void);                               /* -> count */
```

- `owner` tags the entry ("builtin-*" entries survive tui_ext_reset).
- Returns 0 on success, -1 table full / bad args, -2 duplicate name.
- The "System settings" entry registers itself through this API, so
  the main menu and `tui-sys` cannot drift apart.

Example: `tui_menu_register("myapp", "My app", "does my thing",
"[M]", "apps", 0, 30, myapp_main, 0);`

## 7.2 Page extension

Full-screen pages with a generic runner (`tui_ext_page_main`):
Up/Down scroll, Tab cycles registered pages, F1 help, F2 note,
F5 repaint, Esc/F10/q leave. The page draws only body text; the
runner owns clear/focus/title/border/refresh.

```c
int  tui_page_register(const char *owner, const char *name,
                       const char *title, const char *group,
                       int priority,
                       int (*render)(tui_window_t *win, int scroll),
                       int (*input)(int key), void *userptr);
int  tui_page_unregister(const char *name);
int  tui_page_list(char (*out)[TUI_EXT_NAME_MAX], int cap);
int  tui_page_render(const char *name, tui_window_t *win, int scroll);
int  tui_page_input(const char *name, int key);   /* 1 = consumed */
int  tui_ext_page_main(const char *name);         /* generic runner */
```

- `render` returns the number of body lines drawn.
- `input` returns 1 when the key is consumed (the runner's generic
  handling is skipped); return 0 for F1/F2/F5/Esc-style keys.
- Registered pages appear in the `tui-ext` lab and in the Tab cycle.

Builtin pages registered by this batch (all under owner
"builtin-time"/"builtin-sys"/"builtin-ext"):

| Page        | Content                                                 |
|-------------|---------------------------------------------------------|
| time.clock  | live CMOS RTC, UTC + timezone local time, unix seconds, time sources (RTC/HPET/PM timer/TSC), honest NTP status |
| time.tz     | timezone editor (+/- 1h, [/] 15min, s persist, r reset), persisted as `tz.offset` |
| time.set    | form-driven CMOS RTC write (ports 0x70/0x71, RegB SET guard, BCD, 24h kept); SET-window readback proves the registers; QEMU keeps host time after SET clears (documented) |
| sys.host    | hostname / shell.lang / kbd.layout, applied live        |
| sys.net     | IP, mask, gateway, DNS, netfilter INPUT policy toggle   |
| sys.storage | block device count view (parity with lsblk)             |
| sys.user    | honest single-user posture (no /etc/passwd in L0)       |
| sys.security| TOFU/known_hosts, netfilter rules, conntrack            |
| sys.update  | update_url / online_update / A-B slot state             |
| sys.log     | log level 0 (default) / 1 (quiet, failures only)        |
| sys.tui     | theme switcher over the 7.5 registry                    |
| ext         | the extension lab itself (registries + selftest)        |

## 7.3 Widget extension

Rect-scoped widgets with render/event callbacks (text, button,
input, list, table, progress, chart are just `type` strings - the
registry does not police them):

```c
int  tui_widget_register(const char *owner, const char *type,
                         const char *name, int x, int y, int w, int h,
                         tui_attr_t attr,
                         int (*render)(tui_window_t *, int, int, int, int),
                         int (*event)(int), void *userptr);
int  tui_widget_unregister(const char *name);
int  tui_widget_list(tui_widgetext_t *out, int cap);
int  tui_widget_render(const char *name, tui_window_t *win);
int  tui_widget_event(const char *name, int ev);
```

- `event` receives a TUI key code, an ASCII char, or a mouse report
  encoded as (button | action<<8 | x<<16 | y<<24); return 1 if used.

## 7.4 Command extension

TUI-internal commands (the shell has its own registry; this one is
for in-TUI use):

```c
int  tui_cmd_register(const char *owner, const char *name,
                      const char *args, const char *desc,
                      int (*fn)(int argc, char **argv), void *userptr);
int  tui_cmd_unregister(const char *name);
int  tui_cmd_list(tui_cmdext_t *out, int cap);
int  tui_cmd_execute(const char *line);   /* -3 = no such command */
```

Note: the WP-10-tui shell-command registrar was renamed to
`tui_shell_cmds_register` so the documented `tui_cmd_register`
name belongs to this interface.

## 7.5 Theme extension

```c
int  tui_theme_register(const char *owner, const char *name,
                        const char *desc, int fg, int bg, void *userptr);
int  tui_theme_unregister(const char *name);
int  tui_theme_list(tui_themeext_t *out, int cap);
int  tui_theme_set(const char *name);      /* 0 ok */
const char *tui_theme_current(void);
```

- Builtin themes: `default` (silver on black), `green`, `amber`.
- `tui_theme_set` applies immediately (tui_core_apply_theme clears
  and repaints the live stack) and new sessions inherit it.
- The sys.tui page and the ext lab page cycle themes with `t`;
  persist the choice as `tui.theme` (shared shell key).

## 7.6 Hotkey extension

```c
int  tui_key_register(const char *owner, int key, int mods,
                      const char *name, const char *desc,
                      int (*fn)(void), void *userptr);
int  tui_key_unregister(const char *name);
int  tui_key_list(tui_keyext_t *out, int cap);
int  tui_key_conflict(int key, int mods);  /* -1 free, else index */
int  tui_key_dispatch(int key, int mods);  /* 1 = consumed */
```

- Registration fails with -2 on conflict (same key+mods).
- Builtin: F5 repaint.
- `mods`: 0 none, 1 shift, 2 alt, 4 ctrl.

## 7.7 Event extension

```c
int  tui_event_register(const char *owner, const char *type,
                        const char *name,
                        int (*handler)(const char *type, void *payload),
                        void *userptr);
int  tui_event_unregister(const char *name);
int  tui_event_list(tui_eventext_t *out, int cap);
int  tui_event_dispatch(const char *type, void *payload); /* handlers run */
```

- `type` is any string; builtin constants: TUI_EVT_KEYBOARD,
  TUI_EVT_MOUSE, TUI_EVT_TIMER.
- Dispatch returns the number of handlers run.

## Shell/TUI parity map (fix4 4.2.4)

| Setting      | lib_config key | Shell side                | TUI side          |
|--------------|----------------|---------------------------|-------------------|
| hostname     | sys.hostname   | config get/set            | sys.host page     |
| shell lang   | shell.lang     | lang zh/en                | sys.host `l`      |
| kbd layout   | kbd.layout     | config set                | sys.host `k`      |
| timezone     | tz.offset      | config set                | time.tz page      |
| log level    | log.level      | config set                | sys.log page      |
| update url   | update_url     | update/checkupdate        | sys.update page   |
| online check | online_update  | config set                | sys.update `o`    |
| TUI theme    | tui.theme      | config set                | sys.tui `t`       |
| TUI backend  | tui.backend    | config set                | (read at tui_init)|

Both frontends read the same keys, so switching between `tui` and
`exit` never loses data.

## Self-test

`tui-ext` opens the lab page; press `r` to run the selftest. The
seven groups print input/expected/actual/verdict lines in the boot
log style and the page records `last selftest: ALL PASS` (verified
on-screen, see WORK_LOG/STATUS). `tui-ext-reset` semantics: every
non-builtin entry is dropped by `tui_ext_reset()` between sessions.
