<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: writing a module, end to end

A complete walk: source -> surface calls -> load -> run.

## Step 1 - pick your loading path

| Path | When | How |
|------|------|-----|
| Compiled-in | your module ships with a kernel build | compile in, call your init from kmain or a shell built-in |
| Shared object | deploy without rebuilding | build a .so, load through the ld.so surface |
| Embedded tool | ship an ELF payload | `tool_register(name, elf, size)` then `run name` |

## Step 2 - the surface you build on

For a module with a shell command and some work:

```
#include "l1_ext.h"          /* shell bridge, config, ... */
#include "l1_wp8cd.h"        /* shell_register_builtin, jobs */

static int cmd_mymod(const char *args) {
    /* parse args, do the work, print through the console hook */
    return 0;
}

void mymod_init(void) {
    l1_ext_shell_register_command_ex(
        "mymod", cmd_mymod, "my module - does the thing", "L1-demo");
}
```

For UI: add `tui_menu_register` / `tui_page_register`. For storage:
`driver_block_register`. See the family pages.

## Step 3 - build

- Compiled-in: your .c lands in the matching tree directory; the
  Makefile picks it up by wildcard; register from `mymod_init`.
- Shared object: build an ELF .so against the surface headers, then
  in-guest (or from your loader) `so_load(name, flags)` +
  `symbol_resolve`/`dlsym_impl` for the init symbol - the full
  mechanics in [solib-loader.md](solib-loader.md).

## Step 4 - test it

- `run <yourprog>` / invoke your command; check `help -w` shows your
  tag.
- The full bar from [CONTRIBUTING.md](../root/CONTRIBUTING.md): the
  build stays 0-warning, the regression suite still passes 28/28,
  the command count baseline grows by exactly your commands.
- `l1test` and `l1_ext_self_test()` exercise the surface itself.

## Rules of the road

- ASCII strings only for everything the system prints.
- Bounded tables get named limit constants + loud refusal.
- The docs: your command pages regenerate via `make api-docs`; the
  manual chapter for your domain is part of the PR.
