<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-10-wp08fix1 Extension Interfaces (Line Editing + Editor)

**WP-10-wp08fix1 adds 9 new L1 extension points (items 130-138). Total L1
surface is now 138.**

This work package completes the WP-08 shell specification: full-featured
line editing for the `oc>` kernel shell (Up/Down history, Left/Right/
Home/End cursor movement, Tab completion, Ctrl+C/A/E/U/K/W, Delete), the
same key set for the `ush` user shell, the missing `ush` toolset (ln,
chmod, chown, sed, awk, ping, wget, netstat, ifconfig, ps, kill, top, du
- plus the `stat` and `env` commands that `help` had always promised but
no dispatcher ever implemented), and a nano-style editor for both shells.

All previous interfaces remain unchanged. The new headers
`shell/shell_lineedit.h` and `shell/editor.h` are self-contained and are
committed under the project naming convention
(`[category]_[specific]_[smaller]`).

## 1. Kernel-shell line editor (shell/shell_lineedit.h)

The engine is a reusable session struct (`shell_lineedit_t`) holding the
input buffer, cursor, prompt and a 32-entry history ring. The kernel
shell owns one global session; every documented item below operates on
that global session. Rendering uses only `\r` / reprint / `' '` / `\b`
(the framebuffer text console has no ANSI escape support), so the serial
mirror stays byte-identical.

Interactive keys handled by `shell_lineedit_readline()`:

| Key              | Action                                   |
|------------------|------------------------------------------|
| Up / Down        | history paging (draft line preserved)    |
| Left / Right     | move cursor inside the line              |
| Home / End       | jump to line start / end                 |
| Ctrl+A / Ctrl+E  | line start / end                         |
| Backspace / Del  | remove char before / at cursor           |
| Ctrl+U / Ctrl+K  | kill to line start / kill to line end    |
| Ctrl+W           | kill previous word                       |
| Ctrl+C           | cancel line (prints `^C`, empty line)    |
| Tab              | command-name / VFS-path completion       |
| Enter            | finish line (adds to history)            |

### item 130 — `void shell_lineedit_init(void)`

One-time init of the global kernel-shell session: clears the buffer,
cursor, history ring and installs the builtin completer. Called by
`interactive_loop()` in `kernel/main.c` before the first prompt; safe to
call again (resets everything).

### item 131 — `void shell_lineedit_history_add(const char *cmd)`

Append `cmd` to the global session's history ring. Skips empty lines and
consecutive duplicates (bash-like). When the 32-entry ring is full the
oldest entry is dropped. `shell_lineedit_readline()` calls this
automatically on ENTER; L1 code may add entries directly.

### item 132 — `const char *shell_lineedit_history_get(int idx)`

Return history entry `idx` (0 = oldest) from the global session, or
NULL when out of range/empty. `int shell_lineedit_history_count(void)`
returns the live entry count (0..32).

### item 133 — `void shell_lineedit_cursor_move(int dir)`

Programmatic cursor move for the most recent readline context. `dir` is
one of `SHELL_LINEEDIT_LEFT (-1)`, `SHELL_LINEEDIT_RIGHT (1)`,
`SHELL_LINEEDIT_HOME (-2)`, `SHELL_LINEEDIT_END (2)`; any other value
moves by that many steps. No-op when no readline is in progress. The
line is redrawn after the move.

### item 134 — `int shell_lineedit_tab_complete(void)`

Run one Tab-completion round on the global session using the installed
completer. Returns 1 when text was inserted, 0 when nothing matched.

### item 135 — `int shell_lineedit_ctrlc(void)`

Returns 1 when the last (or currently cancelled) input line was aborted
with Ctrl+C and clears the flag; 0 otherwise. Lets L1/commands detect
user cancellation.

### Supporting API (same header)

* `int shell_lineedit_readline(char *buf, int cap)` — blocking,
  interruptible full-screen line read for the `oc>` prompt. Returns the
  line length (0 for a Ctrl+C-cancelled line).
* `void shell_lineedit_set_completer(shell_lineedit_completer_fn fn)` —
  install a custom completer (NULL disables completion).
* `int shell_lineedit_default_completer(const char *line, int cursor,
  char *out, int out_cap)` — builtin completer: first word → shell
  command table (via `shell_complete_command_prefix()` in shell.c),
  later words → VFS path completion via `fs_vfs_readdir()`.
* `int shell_complete_command_prefix(const char *prefix, char *out,
  int out_cap)` (shell/shell.h-compatible, defined in shell.c) — longest
  common prefix over the registered command table; returns the candidate
  count (0 = none, 1 = unique).
* Session engine for embedders: `shell_lineedit_session_init()`,
  `shell_lineedit_session_set_prompt()`,
  `shell_lineedit_session_readline()`.

## 2. nano-style editor (shell/editor.h)

The editor loads the whole file into an array of lines (512 lines × 256
bytes) and drives a per-line interactive editor on the console. File I/O
goes through the real VFS (`fs_vfs_open/read/write`), so the editor
works on any mounted filesystem (ramfs, FAT32, exFAT, ext4).

Key bindings: Up/Down select line, Left/Right/Home/End (and Ctrl+A/E)
move inside it, typing inserts at the cursor, Backspace/Delete delete,
ENTER splits the line at the cursor (nano behaviour), Ctrl+K deletes the
line, Ctrl+C reports the position, Ctrl+G shows the key guide, Ctrl+O
saves, Ctrl+X exits (asks before discarding unsaved changes).

Shell: `nano <file>` and `vi <file>` (same engine, both names) on `oc>`,
plus `nano`/`vi` builtins in `ush`. The WP-05 `edit` line editor remains
available unchanged.

### item 136 — `int editor_open(const char *file)`

Load `file` into the editor session (an empty new session when the file
does not exist yet). Returns 0 on success, negative on error:
-1 bad args, -2 session already active, -3 out of memory, -4 read error.

### item 137 — `int editor_save(void)`

Write the current session content back to the file the session was
opened with (open with O_TRUNC, then write line by line with trailing
newlines). Returns 0 on success, negative on error.

### item 138 — `int editor_close(void)`

Drop the session and free the state. Unsaved changes are DISCARDED — the
interactive loop asks for confirmation before calling this. Returns 0 on
success. (`int editor_is_active(void)` and
`const char *editor_file(void)` report the session status.)

## 3. ush-side additions (user space, ring 3)

The user-space counterparts live in `userprogs/ush.c` and use seven new
syscalls (96-102) documented in `kernel/core/core_syscall.h`:

| Syscall | Name       | Purpose                                          |
|---------|------------|--------------------------------------------------|
| 96      | SYS_SYMLINK  | create a symlink (fs_vfs_symlink)             |
| 97      | SYS_READLINK | read a symlink target (fs_vfs_readlink)       |
| 98      | SYS_LINK     | create a hard link (fs_vfs_link)              |
| 99      | SYS_CHMOD    | change permission bits (fs_vfs_chmod)         |
| 100     | SYS_CHOWN    | change owner (fs_vfs_chown)                   |
| 101     | SYS_NETCMD   | run ifconfig/ping/netstat/wget in the kernel via `shell_execute_captured()` and capture the REAL output |
| 102     | SYS_PS       | copy the live scheduler task table (core_sched_task_info_get) |

New ush builtins: `ln`, `chmod`, `chown`, `sed`, `awk`, `ping`, `wget`,
`netstat`, `ifconfig`, `ps`, `kill`, `top`, `du`, `nano`/`vi`, plus the
`stat` and `env` commands that were listed in `ush> help` since WP-08
but never implemented. The ush prompt gained the full editing key set
identical to `oc>` (its own ring-3 implementation over SYS_GETCH, no
kernel headers).

## 4. VFS additions (fs/fs_vfs.h)

`fs_vfs_stat_t` grew `mode/uid/gid/nlink` (mirrored in
`userprogs/ush.c` `struct ush_stat`; the layout is a user-kernel ABI —
SYS_STAT copies `sizeof(fs_vfs_stat_t)` bytes). `fs_vfs_node_t` carries
`mode/uid/gid/nlink` with defaults `0644`/`0755`, uid 0, gid 0, nlink 1.
New calls: `fs_vfs_link()`, `fs_vfs_symlink()`, `fs_vfs_readlink()`,
`fs_vfs_chmod()`, `fs_vfs_chown()`, plus `VFS_TYPE_SYMLINK` and three
new dir_ops callbacks (`link`, `symlink`, `readlink`) implemented by
ramfs; other filesystems return "unsupported" honestly. Hard links share
the ramfs inode and the nlink count is real (unlink only frees the inode
with the last link). Path resolution follows symlinks with an 8-hop
ELOOP limit (absolute targets).
