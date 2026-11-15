<!-- SPDX-License-Identifier: Apache-2.0 -->
# The TUI desktop

## Starting it

```
ush> tui
```

The desktop takes the graphical console (run QEMU with `-vga std`);
the serial line keeps the ush prompt for parallel access.

## The screen

- the **panel** at the top: clock, power, system summary
- the **desktop** center: the app launcher
- the **taskbar**: running TUI apps, the launcher button

Keyboard model: arrows + Enter navigate, ESC backs out, the menu
key opens the launcher. The mouse is not required - every action is
reachable from the keyboard (the input contract the accessibility
gate asserts).

## The built-in TUI apps

| App | What it does |
|-----|--------------|
| tui-files | browse / copy / move / delete, launch edit |
| tui-editor | the same editor core the `edit` command uses, framed |
| tui-proc | the process table (the `ps` data) |
| tui-apps | the app launcher / registry browser |
| tui-time | clocks (RTC / HPET / TSC) |
| tui-net | network status surfaces |
| tui-sysinfo / tui-hw / tui-drv / tui-sys | hardware + system pages |

## The TUI command family

`tui` (desktop), `tui-test` (the selftests), `tui-hw`, `tui-net`,
`tui-sysinfo [page]`, `tui-time`, `tui-drv [page]`, `tui-sys [page]`, `tui-ext` - all registry programs, all also reachable
through the shell while the desktop runs.

## TUI and the merged apps

The TUI renders the same services the 8 merged apps wrap - the
sysinfo pages read the same L1SVC ops `hwinfo` does. The desktop is
a viewer; the wizard apps are the operator surface; the classic
commands are the scripting surface - three doors, one system.
