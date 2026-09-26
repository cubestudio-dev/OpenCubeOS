<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# WP-08 L1 Extension Interfaces — User-space Shell + Toolset (Items 51-57)

**Work Package**: WP-08 — User-space Shell + Toolset
**Total L1 Interfaces**: 7 (items 51-57)
**Status**: Complete
**License**: Apache 2.0

## Overview

WP-08 adds 7 L1 extension interfaces for user-space shell, tools, job control, and signal handling.

## Interface 51: shell_run

```c
int shell_run(void);
```

Launch the user-space shell (ush). Blocks until ush exits.

**Returns**: 0 on normal exit, -1 on failure.

## Interface 52: shell_register_builtin

```c
typedef int (*shell_builtin_fn)(int argc, char **argv);
int shell_register_builtin(const char *name, shell_builtin_fn fn, const char *help);
```

Register a built-in command in the user-space shell.

## Interface 53: tool_register

```c
int tool_register(const char *name, const u8 *elf_data, u64 elf_size);
```

Register a user-space tool that can be exec'd by the shell.

## Interface 54: tool_list

```c
int tool_list(char *buf, int bufsize);
```

List all registered tools.

## Interface 55: job_create

```c
int job_create(const char *cmd);
```

Create a background job (fork without wait).

## Interface 56: job_list

```c
int job_list(char *buf, int bufsize);
```

List all background jobs.

## Interface 57: job_control

```c
int job_control(int job_id, int action);
```

Control a job: 0=fg, 1=bg, 2=kill.

## ABI Stability

All interfaces are stable. WP-01..WP-08 interfaces remain backward compatible.

## Total L1 Surface

57 interfaces: WP-01..07 (32) + WP-08 (25 = 8+10+7) = 57.
