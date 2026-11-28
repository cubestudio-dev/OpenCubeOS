<!-- SPDX-License-Identifier: Apache-2.0 -->
# ush — the complete shell reference

`ush` is the ring-3 shell and the default boot entry (the kernel's
`l1_launcher_boot_default()` spawns it; when it exits, L0 halts). It
lives at `ocos/l1apps/ush/main.c` — a raw ELF (the only non-gzipped
registry program), using SYS_READLINE(73)/SYS_GETCH(74) for input,
fork+execve+wait4 for programs, SYS_PIPE(20)+SYS_DUP2(51) for pipes,
SYS_OPEN/CLOSE for redirection.

## The 12 built-ins

Only these twelve words are interpreted by the shell itself. Everything
else is a registry program.

| Built-in | Syntax | Behavior |
|----------|--------|----------|
| `cd` | `cd [dir]` | change the shell's working directory; no argument = `/` |
| `help` | `help [-a\|-w]` | built-in summary + registry count line; `-a`/`--all` = full A-Z listing, `-w`/`--wp` = grouped by work package (mega group first) |
| `export` | `export VAR=val` | set an environment variable (32 vars max, name ≤ 32, value ≤ 128 bytes) |
| `alias` | `alias name=val` / `alias name value...` | set an alias (16 max, name ≤ 32, value ≤ 64); bare `alias` lists all; `alias name` prints one |
| `unalias` | `unalias name` | remove an alias; prints a usage error when called without an argument |
| `env` | `env` | print all environment variables |
| `set` | `set [VAR]` | bare `set` shows variables; `set VAR` prints `VAR=value` |
| `history` | `history` | numbered history, 1..n |
| `jobs` | `jobs` | list background job table entries |
| `fg` | `fg [job]` | **not available** — prints `ush: job control requires external programs` |
| `bg` | `bg` | **not available** — same message |
| `exit` | `exit` | **refused** — `ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart` |

Notes:

- `fg`/`bg` are recognized words but job **resume** is not implemented.
  `jobs` listing and `cmd &` background launch ARE real (16 job slots;
  the parent prints `[n] <pid>`).
- `exit` refuses because there is nothing to exit to — ush IS the boot
  entry; its exit is end-of-session (the kernel prints `l1 launcher: default L1 app exited; L0 halting`).
- The words that used to be built-ins in v2.x (`ls`, `cat`, `wc`,
  `grep`, `head`, `tail`, `mkdir`, `rmdir`, `touch`, `rm`, `cp`, `mv`,
  `sort`, `uniq`, `ps`, `top`, `kill`, `date`, `uname`, `free`, `df`,
  `stat`, `du`, `ln`, `chmod`, `chown`, `sed`, `awk`, `ping`, `wget`,
  `netstat`, `ifconfig`, `nano`/`vi`/`edit`) are now ordinary apps
  reached via execve.

## The line editor

All editing happens on raw SYS_GETCH codes; the line cap is 511 bytes.

| Key | Code | Action |
|-----|------|--------|
| Up | 0x80 | previous history entry (draft preserved — see below) |
| Down | 0x81 | next history entry; past the newest restores your draft |
| Left / Right | 0x82 / 0x83 | glyph-wise cursor motion (UTF-8 aware: `u8_prev`/`u8_next`) |
| Home / Ctrl+A | 0x84 / 0x01 | line start |
| End / Ctrl+E | 0x85 / 0x05 | line end |
| Delete | 0x89 | delete glyph at cursor |
| Backspace | 0x08 / 0x7F | delete glyph before cursor |
| Ctrl+U | 0x15 | kill to line start |
| Ctrl+K | 0x0B | kill to line end |
| Ctrl+W | 0x17 | kill previous word |
| Ctrl+C | 0x03 | `^C` — cancel the line |
| Tab | 0x09 | completion (see below) |
| 0x100..0x1FF | — | serial-pasted UTF-8 bytes: the COM1 bridge's +0x100 bias is stripped, so pasted CJK accumulates byte-by-byte |
| printable ASCII | — | insert at cursor |

**History**: 16 entries, 256 bytes each, consecutive duplicates
deduplicated, FIFO slide when full. `history` prints them numbered.

## Tab completion — registry-driven

Candidates are the **live registry** — the exact text `help` prints,
fetched through the L1SVC `l1/apps` op (footer `--` lines skipped, cap
290 entries) — plus the 12 true built-ins.

- Single match: completes the word and appends a space.
- Multiple matches: completes the common prefix.
- No command match and the word contains `/` (or is any later word on
  the line): **VFS filename completion** kicks in — the directory part
  is read via sys_readdir (64 entries scanned, common-prefix logic,
  bounded appends).

Completion never enumerates command *arguments* — subcommand
grammars are documented in
[function-dictionary.md](function-dictionary.md).

## Pipes, redirection, background

| Feature | Syntax | Limits |
|---------|--------|--------|
| Pipeline | `cmd1 \| cmd2 \| ...` | max **8 segments**; pipes created up front, one fork per segment; the first child keeps console stdin, the last keeps console stdout |
| Redirect out | `cmd > file` (truncate), `cmd >> file` (append) | parsed anywhere in the command; originals dup()'d and restored; built-ins DO see redirects |
| Redirect in | `cmd < file` | same mechanism over fd 0 |
| Background | `cmd &` | fork without wait; job table entry; prints `[n] <pid>` |

An empty redirect target is a syntax error: `ush: syntax error near '>' ` (and `near '<'`).

## Variable substitution — and what ush does NOT have

`$VAR` expansion is the **only** substitution (`expand_env`, 512-byte
result; `echo $PATH`, `export X=...; run echo $X` work).

The shell deliberately does **not** implement:

- **wildcards/globbing** — `ls *` passes the literal `*` to `ls`, which
  also has no glob. No `?`, no `[...]` anywhere.
- `&&` / `||` chaining
- `$?` and any other special variables
- quoting (no `'...'` / `"..."`)
- heredocs
- functions, arithmetic, loops — scripting is one command per line

Commands not found print `ush: command not found: <cmd>`; fork failures
print `ush: fork failed`; alias expansion that overflows prints `ush: alias expansion too long`.

---

## The editors

One shared engine (`ocos/interface/l1_editor_core.h`), three faces:
**nano** and **edit** are byte-for-byte the same nano-style face (edit
is the v2.2.0 spelling); **vi** is a real modal layer over the same
engine. Limits: 256 lines × 128 bytes/line; a file past the limits
loads partially (`unano_file_truncated` / `partial load`) and **save
refuses to overwrite** — the status bar shows `[PARTIAL: save refused]`.

### nano / edit keys

| Key | Action |
|-----|--------|
| arrows | glyph-wise motion |
| typing | inserts UTF-8 |
| Backspace / Delete | remove whole glyphs |
| ENTER | split the line |
| **F2** | toggle the **system IME** (pinyin; the engine lives in the L0 display service — the editor only toggles + feeds keys + renders the preedit strip). While composing: digits 1-9 pick a candidate, space = first, PgUp/PgDn or `-`/`=` page, Backspace drops a letter, Enter commits the raw letters, Esc cancels |
| Ctrl+K (0x0B) | cut line |
| Ctrl+R (0x12) | copy line |
| Ctrl+U (0x15) | paste the clipboard line below (UTF-8-safe, one-line clipboard) |
| Ctrl+C (0x03) | report cursor position |
| Ctrl+O (0x0F) | save |
| Ctrl+X (0x18) | exit (asks when dirty) |
| Ctrl+G | help |

The status bar is a differential pinned bottom row (ANSI
`\033[s\033[999;1H\033[K...\033[u`; zero output when unchanged):
`[* ]file 1/3 IME:on [preedit]`.

### vi keys

COMMAND mode (default):

| Key | Action |
|-----|--------|
| `h j k l` / arrows | motion (h/l glyph-wise) |
| `0` / `$` | line start / end |
| `x` | delete glyph at cursor |
| `dd` | cut line |
| `yy` | copy line |
| `p` | paste below |
| `i` / `a` / `o` | enter INSERT (at cursor / after it / new line below) |
| `:` | ex line: `:w` save, `:q` quit, `:wq` both, `:q!` discard |

INSERT mode: typing inserts (UTF-8 glyph-aware), Backspace/Delete
remove whole glyphs, **F2 toggles the system IME** (same engine as
nano), `Esc` returns to COMMAND. Status bar: `[* ]file 1/3 CMD|INSERT`
+ preedit strip.

---

## The TUI family and its hotkeys

`tui` (desktop) and the seven page apps (`tui-sys`, `tui-sysinfo`,
`tui-hw`, `tui-drv`, `tui-net`, `tui-time`, `tui-ext`) render
full-screen and bypass the text capture (their output never reaches an
L1SVC capture buffer). `tui-test` serves its verdict through the
capture. All pages share one key loop:

| Key | Action |
|-----|--------|
| `Tab` | next page (tab bar shows `[current]`) |
| `Up` / `Down` | scroll the page (long pages such as PCI caps can be read past the first screen) |
| `+` / `=` | volume +10 (Sound page; persists `snd.volume` to config) |
| `-` | volume -10 |
| `F1` | help (the shared sysinfo help screen) |
| `F2` | save config (`/etc/opencube.conf` or defaults) |
| `F5` | repaint |
| `Esc` / `F10` / `q` | leave the page, back to the prompt |

## Recipes

```
ush> help -w                          # find commands by group
ush> top -n 5                         # five refreshes, then exits
ush> cat /etc/opencube.conf           # no pager; pipe into head
ush> dmesg 100 > /tmp/boot.txt        # capture the last 100 klog lines
ush> grep dhcp < /tmp/boot.txt        # grep filters stdin
ush> play 440 &                       # background job: [1] 12
ush> jobs                             # see it
ush> ime -i                           # interactive pinyin compose line
```

## Related pages

- [function-dictionary.md](function-dictionary.md) — all 311 commands
- [shell-basics.md](shell-basics.md) — the beginner's tour
- [getting-started.md](getting-started.md) — first boot
- [../troubleshooting/silent-failures.md](../troubleshooting/silent-failures.md) —
  why some console output never appears (capture routing)
