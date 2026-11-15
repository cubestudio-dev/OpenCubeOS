<!-- SPDX-License-Identifier: Apache-2.0 -->
# Shell basics

`ush` is the boot shell - a real ring-3 program (the only
`OC_L1_APP_DECL` application) that fronts the whole system.

## The prompt

```
ush>
```

## Running programs

Type a registry program name (284 available - `help -a` lists them
all). The 8 merged apps are the intended surface; the historical
single commands keep working:

```
ush> hwinfo
ush> lsblk
ush> run hello &            (the '&' suffix runs in background)
```

`run <program>` is the explicit form - it prints the started pid and
waits (or backgrounds with `&`).

## Pipes and redirection

```
ush> cat /etc/opencube.conf | grep url
ush> help -a > registry.txt
ush> echo hello >> notes.txt
ush> wc < notes.txt
```

Pipes (`|`), truncating redirect (`>`), appending redirect (`>>`)
and input redirect (`<`) work across registry programs.

## Built-ins vs programs

Only process-state commands are built into ush: `cd`, `pwd`,
`exit`, `help`, `export`, `alias`, `unalias`, `history`, `lang`,
`clear`. Everything else - including `ls`, `cat`, `echo`, `write` -
is a forked registry program. That is why `ls` appears in pipes as
a real process.

The line editor: Up/Down history, Tab completion of program names,
Ctrl+C cancels the line.

## Aliases and environment

```
ush> alias dt disk-tool
ush> alias
ush> unalias dt
ush> export EDITOR=edit
```

## Scripting

Multi-command sequences with `;`:

```
ush> dhcp; ping 10.0.2.2; dns example.com
```

There is no file-based script interpreter in this release - a
"script" is a pasted sequence of lines, or a program you write into
the registry at build time (see the developer docs).

## Language

```
ush> lang zh        (the whole UI - shell help + every wizard -
                     switches to Chinese live)
ush> lang           (shows the current language)
```

## The mega-app contract (recap)

```
ush> <app>                wizard (menus, single-key picks, q quits)
ush> <app> direct <op>    explicit verbatim passthrough
ush> <app> <op> [args]    classic dispatch (historical spelling)
```

See [mega-apps](mega-apps.md) for the full contract and per-app
walkthroughs.
