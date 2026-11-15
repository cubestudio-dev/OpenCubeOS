<!-- SPDX-License-Identifier: Apache-2.0 -->
# Writing an app - from source to `run`

A complete walk: we add a `sysdiag`-style op wrapper, then extend it
into a mini-wizard. Everything here is mechanical - no tables to
register by hand.

## Step 1 - the folder

```
ocos/l1apps/hello2/
  main.c
  i18n/en.json
  i18n/zh.json
```

## Step 2 - the minimal app (a verbatim service wrap)

`main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
#include "l1_lib.h"
#include "l1svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_DIAG, L1SVC_DIAG_SCHED); }
```

`i18n/en.json`:

```json
{
  "_hint": "scheduler stats, verbatim",
  "svc_call_failed": ": service call failed (rc=-1)\n"
}
```

`i18n/zh.json` mirrors every key (Chinese text, same keys).

## Step 3 - build and run

```
make userprogs kernel iso     # or: make all
make run-bios
ush> hello2                   (the scheduler stats appear)
ush> run hello2               (same thing, explicitly)
```

`tools/gen_userprogs.py` discovered the folder, compiled it, and
the registry grew by one (`help -a` shows `hello2 [l1]`).

## Step 4 - give it a wp tag

`tools/gen_userprogs.py` `WP_TAGS` - add `"hello2": "core",`
(alphabetical). The tag drives `help -w` grouping. Unknown names
default to "l1", which is honest but ungrouped.

## Step 5 - make it a wizard

```c
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

static char g_out[WIZ_OUT_CAP];

static int hello2_wizard(void) {
    static const char *const items[] = { "h2_sched", "h2_mem" };
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 2);
        if (c == 0) return 0;
        (void)wiz_svc(c == 1 ? L1SVC_CAT_DIAG : L1SVC_CAT_DIAG,
                      c == 1 ? L1SVC_DIAG_SCHED : L1SVC_DIAG_MEM,
                      0, g_out);
        wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return hello2_wizard();
    return l1svc_main(argc, argv, L1SVC_CAT_DIAG, L1SVC_DIAG_SCHED);
}
```

plus the `wiz_*` keys in both json files. That is the entire
three-door pattern: no-arg wizard, classic verbatim, and (if you
want the explicit form) a `direct` prefix skip - copy the dispatch
shape from `ocos/l1apps/disk-tool/main.c`.

## Step 6 - the gates your app must pass

- `make all` with `-Wall -Wextra -Werror` (freestanding, no
  stack protector, no SSE) - zero warnings, always.
- Both i18n files present and complete (the build fails otherwise).
- The staleness gate: editing any source or json under the folder
  regenerates `userprogs_data.h` on the next `make all` - never
  commit a stale embedded header.
- The interface-freeze rule: ADD programs, never remove them
  ([CONTRIBUTING.md](../root/CONTRIBUTING.md)).
- If you touched L0 service output lines, the anchors
  ([surface-map.md](surface-map.md)) and the E2E suites are part of
  your change.

## Step 7 - test it like the project does

```
python3 tools/run_fix4_regression.py build/opencube.iso log.txt
```

The gate boots the ISO, runs the command list, asserts the
per-command patterns and the registry floor (>= 276). The mega-app
era adds `tools/` E2E scripts for wizard flows (see
[../dev/e2e-suites.md](../dev/e2e-suites.md)).
