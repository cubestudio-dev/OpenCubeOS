<!-- SPDX-License-Identifier: Apache-2.0 -->
# Shell basics

The `oc>` shell is a Unix-flavoured command line - with pipes,
redirection, background jobs, environment variables, aliases and a
small scripting language. This page walks the real feature set.

## Line editing

- History: up/down arrows walk previous lines.
- Editing within the line; `Ctrl+U` kills the line.
- Input sources: PS/2 keyboard and COM1 serial feed the same queue -
  with `-serial mon:stdio` in QEMU you type over serial.

## Command chaining and jobs

```
oc> dhcp && ping 10.0.2.2      (run ping only if dhcp succeeded)
oc> cat a.txt ; cat b.txt      (sequence regardless)
oc> health || dmesg            (second only if first failed)
oc> ping 10.0.2.2 &            (background job)
oc> g6_test &                  (long test in the background)
```

## Pipes and redirection

```
oc> ls | grep log              (pipe stdout of ls into grep)
oc> cat /mnt/f.txt > /tmp/g.txt     (write stdout to a file)
oc> cat /mnt/f.txt >> /tmp/g.txt    (append)
oc> wget 10.0.2.2 < in.txt     (stdin from a file)
```

## Wildcards

`*` and `?` expand in the shell before the command runs:

```
oc> ls /mnt/*.txt
oc> cat /mnt/report?.txt
```

## Environment variables

```
oc> export NAME=value          (set)
oc> echo $NAME                 (use - $VAR expands in the line)
oc> env                         (list all)
```

Environment storage limits: 32 variables, names up to 32 bytes,
values up to 256 bytes (`shell/shell.h`).

## Aliases

```
oc> alias ll ls -l
oc> ll
oc> unalias ll
```

Alias values may hold full pipelines - up to 512 bytes stored in
full or refused (never silently truncated). 32 alias slots.

## Current working directory

```
oc> cd /mnt
oc> pwd
```

## Scripting

A script is a text file of command lines; feed it with the shell's
script runner (see `help -a` for the exact command name in your
build). The `editor` command gives you an in-guest full-screen text
editor to write those scripts.

## Command help

```
oc> help            (short list, one line per command)
oc> help -a         (all 233 commands, alphabetical, with descriptions)
oc> help -w         (the same commands grouped by the work package
                     that introduced them)
```

The complete indexed reference with one page per command is
[commands/index.md](commands/index.md).

## The contract

Commands may be added across versions, never removed; names and
overall behaviour of shipped commands are stable (see
[../CONTRIBUTING.md](../root/CONTRIBUTING.md)). The command table
capacity is 256 with 233 registered at present.
