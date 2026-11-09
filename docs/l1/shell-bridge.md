<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: the shell bridge

How module code becomes shell surface.

## Registering a command from L1

```
l1_ext_shell_register_command_ex(name, fn, help, wp_tag)
```

`fn` receives the argument string, returns 0/non-zero. The tag shows
in `help -w`. The table holds 256 commands (233 registered today) -
registration is refused with a clear error when full, never
silently.

## Enumerating (the TUI pattern)

```
l1_ext_shell_list_commands_a_z()
l1_ext_shell_list_commands_by_wp()
```

The TUI command bridge (`tui/tui_cmd.c`) enumerates the table
through exactly these and renders the launcher - the reference
consumer. Any L1 module may do the same.

## Built-ins and jobs (l1_wp8cd.h)

`shell_register_builtin` / `shell_unregister_builtin` (the
self-test path uses these), `shell_run` (the interactive loop),
`job_create / job_list / job_control` for background jobs, and
`tool_register` to embed an ELF payload the shell can `run`.

## The contract

- Names are ASCII, lowercase, dotted/multi-word allowed
  (`mkfs.fat32`, `health export` are real registrations).
- Help is one line; argument errors print `usage: ...`.
- Commands are never removed once shipped
  ([CONTRIBUTING.md](../root/CONTRIBUTING.md)).
