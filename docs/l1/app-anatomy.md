<!-- SPDX-License-Identifier: Apache-2.0 -->
# App anatomy - what a main.c is made of

Every registry program is the same five-part shape. This page walks
the parts using real in-tree examples.

## 1. The folder

```
ocos/l1apps/<name>/
  main.c             app_main(int argc, char **argv)
  i18n/en.json       English strings + the "_hint" registry line
  i18n/zh.json       Chinese mirror (every key present)
```

The folder name IS the program name. Extra `.c` files compile and
link together (multi-file apps are fine). Flat legacy programs
(`ocos/l1apps/*.c`, the v2.x test family) keep their v2.x image
modes; their i18n lives in `ocos/l1apps/_i18n/<name>/`.

## 2. The entry point

```c
#include "l1_lib.h"
#include "l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_OTA, L1SVC_OTA_UPDATE); }
```

That is the complete `update` app of the pre-merge era: the crt0
trampoline (`_shared` style, linked by the build) hands argc/argv
to `app_main`; `l1svc_main()` is the verbatim service-wrap body -
join argv[1..] with single spaces into a 256-byte argument line,
call `sys_l1svc(cat, op, arg, out, 32768)`, print `out`, and on
failure print `<name>` + `tr_("svc_call_failed")`.

## 3. The i18n resources

```json
{
  "_hint": "check for OTA updates",
  "svc_call_failed": ": service call failed (rc=-1)\n"
}
```

- `_hint` is reserved: the registry/help line (consumed at build
  time by `tools/gen_userprogs.py`).
- Every other key is runtime text: the app says `tr_("key")`, the
  active locale (`lang en|zh`, probed live per call) picks the
  table, misses fall back en -> the key itself. An untranslated app
  can never crash - it shows the key, visibly.
- Tables are emitted key-sorted into
  `build/userprogs_i18n/<name>_i18n.c`; `tr_()` binary-searches
  them.
- The build FAILS on a missing en.json or zh.json.

## 4. Doing more than wrapping

When an app needs interaction, it has two input primitives:

- `syscall2(SYS_READLINE, buf, cap)` - blocking line input with
  echo + backspace (max 255, returns the length, NUL-terminates);
- `sys_getch()` - non-blocking single raw key (-1 when none) - the
  ush spin-wait pattern for menus.

And one launcher pattern (fork + execve + wait4) - see
`ocos/l1apps/run/main.c` and the `files` mega-app for the defensive
spellings (static argv storage, explicit child exit).

## 5. The wizard layer

```c
#include "wizard_lib.h"

static char g_out[WIZ_OUT_CAP];      /* 32 KiB, BSS not stack */

int app_main(int argc, char **argv)
{
    if (argc < 2) return my_wizard();          /* the intent door */
    ...
    return l1svc_main(argc, argv, CAT, OP);    /* the classic door */
}
```

`wizard_lib.h` (header-only, every app compiles its own copy)
provides: `wiz_svc` / `wiz_svc_quiet` (L1SVC with a 32 KiB static
buffer), `wiz_find` (anchor probe), `wiz_token_after` (word after a
key - the IP extractor), `wiz_ask` / `wiz_confirm` / `wiz_pause`
(READLINE-based), `wiz_menu` (single-key 1..9 menus, q/ESC quit),
`wiz_banner` / `wiz_step` / `wiz_ok` / `wiz_fail`. Full contract:
[wizard-library.md](wizard-library.md).

## 6. Registering

Nothing to do: `make userprogs` (part of `make all`) discovers the
folder, compiles it, emits the blob + the `OC_USERPROGS[]` row
(name, wp tag, hints, image), and the kernel's
`core_userprog_lookup()` serves execve by name. Add the wp tag in
`tools/gen_userprogs.py` `WP_TAGS` (unknown names default to
"l1").
