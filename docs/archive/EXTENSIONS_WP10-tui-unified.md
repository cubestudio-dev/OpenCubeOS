# WP-10-tui-unified - Extension interfaces

Batch: WP-10-tui-unified (unified TUI desktop + full character
desktop + extensible L1 applications)
Author: cubestudio-dev <cubestudio@qq.com>
License: Apache-2.0

This work package ships one big idea in four parts:

1. **One desktop entry**: `tui` is the single TUI entry point.  The
   old `tui-cn` launcher is REMOVED (command count 225 -> 224; the
   `tui-*` application commands stay).  `tui` adapts to `shell.lang`
   (en/zh labels), F2 switches language at runtime and persists it
   back into `/etc/opencube.conf`.
2. **A fully dynamic menu tree**: every entry is a node in a runtime
   registry (`tui_dt_node_add/remove/find/depth`), up to
   `TUI_DT_DEPTH_MAX = 6` levels.  The desktop renders whatever is
   registered - L1 code can add or remove applications while the
   desktop runs.
3. **Five desktop applications** over the real subsystems: Files
   (file manager over the VFS), Processes (task manager over
   `g_procs` + PMM), Editor (nano bridge over the shell command
   path), Terminal (ring-3 `ush` with syscall-only confinement),
   and System Info - plus the builtin Control Panel (hardware /
   storage / network / time / update / security / log), Chinese
   Input (pinyin pad), Command Bridge (any shell command with live
   captured output) and the Extensions lab.
4. **Exception isolation at both boundaries**: a page crash is
   contained by the `tui_dt_guard` setjmp barrier (the menu
   survives, the shell is restored), and - new in part 4 - a ring-3
   process can no longer touch I/O ports at all: `priv_test`
   proves `out 0x70` from ring 3 raises #GP(0), the process dies,
   the kernel never panics.

Every interface below has a real implementation and a real test
(`tui-test`, 90/90 checks) plus E2E sessions on BIOS and UEFI.

## Part 1 - the unified desktop entry

`tui/tui_desktop.[ch]`:

```c
int  tui_desktop_main(void);        /* the `tui` command lands here */
void tui_dt_bootstrap(void);        /* idempotent builtin registry   */
int  tui_dt_count(void);            /* registry occupancy            */
```

- language layer: `tui_lang_current()` / `tui_lang_set()` /
  `tui_tr(en, zh)`; `shell.lang` is the single source of truth
  (part 2 removed the drifting copy).
- resident status bar with the per-language hotkey map
  (`Esc:Back 1-9:Quick Up/Down:Move Enter:Open F1:Help F2:Lang
  F5:Refresh F10:Exit`), strong focus highlight (reverse video),
  green OK / red FAIL toasts, loading spinner.
- crash guard:

```c
int  tui_dt_guard(int (*fn)(tui_dt_node_t *), tui_dt_node_t *self);
void tui_dt_panic(int code);        /* nests to the nearest guard   */
```

A runner that panics is contained (rc -1000), reported in a red
toast, and the menu keeps working - verified by tui-test.

## Part 2 - the dynamic registry (6-level menu tree)

```c
int  tui_dt_node_add(const char *parent_id, const tui_dt_node_t *spec);
     /* 0 ok | -1 bad args | -2 duplicate id | -3 full
        -4 parent missing | -5 would exceed depth 6            */
int  tui_dt_node_remove(const char *owner, const char *id);
tui_dt_node_t *tui_dt_find(const char *id);
int  tui_dt_depth(const char *id);  /* 1..6, -1 missing            */
void tui_dt_reset(void);
```

- node kinds: `TUI_DT_MENU` (submenu, optional lazy
  `populate()`), `TUI_DT_PAGE`, `TUI_DT_APP`, `TUI_DT_SHELL`
  (leaves the TUI, runs one command, returns), `TUI_DT_TERMINAL`
  (leaves the TUI, spawns ring-3 ush).
- only the owner (or "builtin") may remove a node.
- the registry is capped (`TUI_DT_MAX 56`, `TUI_DT_CHILD_MAX 16`)
  and lives in the identity-window .bss budget - dynamic subtrees
  (per-disk entries, command-bridge items) reuse it per session.
- depth is enforced: level-6 nodes are accepted, level-7 refused
  (tui-test verifies both).

## Part 3 - the five desktop applications

| Node id   | App          | Real subsystem it drives                            |
|-----------|--------------|-----------------------------------------------------|
| files     | Files        | VFS readdir/stat/open/write/rename/unlink/rmdir, mount table, clipboard (c/x/p), per-directory buffers from the kernel heap |
| procs     | Processes    | live `g_procs` table (pid/ppid/tid/brk/name), real PMM used/total, uptime, auto-refresh, per-process detail, `k` = kill via `user_process_kill_by_tid()` |
| editor    | Editor       | argument form -> the real nano through its live shell command path (`^O` save, `^X` exit), parks the desktop session and re-enters |
| terminal  | Terminal     | parks the desktop, spawns ring-3 `ush` (syscall-only), re-enters when ush exits; privileged instructions from ring 3 are contained (see part 4) |
| sysinfo   | System Info  | overview / processes / logs / monitor / power pages |

Supporting surfaces: Control Panel (Hardware 12 pages, Storage
with live per-disk subtree down at level 6 incl. real block-device
detail + `disk_rw_test`, Network dhcp/ip/route/dns/firewall/tcpstats,
Time&Language, Update, Security, Log), Chinese Input (the pinyin
pad `tui_cn_input_app` - the same surface the old tui-cn launcher
exposed), Command Bridge (`shell_execute_captured` with live
output), Extensions lab (the fix4 `tui_ext` seven interface sets,
selftest reachable in place).

Part 4 also added the **digit quick-open**: keys `1..9` activate
the Nth child of the current menu level (root quick-launch; the
same semantic as arrows + Enter).  The status bar advertises it
(`1-9:Quick`).  Deterministic for E2E drivers, and a standard
desktop accelerator.

## Part 4 - ring-3 I/O containment (the PRIV_TEST root cause)

`userprogs/priv_test.asm` executes the privileged instruction
`out %al, $0x70` (CMOS port) from ring 3.  The expected behavior:
#GP(0), the kernel kills the process, the shell survives.

The first run of this test **failed open** - the instruction
executed.  Root cause (a real kernel bug since WP-02, caught by
this work package's own new test): `arch_tss_t` placed the
`iopb_offset` field at TSS offset **0x64**, but SDM table 8-2 puts
the I/O-permission-bitmap offset field at **0x66**; the CPU (and
every honest emulator) therefore read the pad word (0) as the
bitmap offset - the "bitmap" started at TSS byte 0, over the
all-zero header, and every port was allowed from ring 3.

Fix (kernel/arch/x86_64/arch_idt.c):

- field order corrected (`reserved5` u16 at 0x64,
  `iopb_offset` u16 at 0x66 - a `offsetof` macro was added to
  kernel/types.h),
- a 32-byte all-ones I/O permission bitmap now lives INSIDE the
  TSS limit (ports 0..255 denied; ports beyond the bitmap fall
  past the TSS limit and are denied by the same rule on real
  hardware),
- `memset(g_tss.iopb, 0xFF, sizeof(g_tss.iopb))` at tss_init.

Evidence (BIOS and UEFI):

```
priv_test: attempting out 0x70 from ring 3
*** EXCEPTION 13: #GP General Protection ***
  error_code=0x0 rip=0x40001A cs=0x33 rflags=0x10246
*** KILLING user process (pid=1) due to exception 13, kernel continues ***
[user] process killed by exception (vector=13), kernel continues
echo alive-after-gp
alive-after-gp
```

`run priv_test` is registered in the `run` command (the payload is
embedded as `userprog_priv_test`, ELF64 header checked by
tui-test).

## L1 API summary added by this work package

| API | Purpose |
|-----|---------|
| `shell_cmd_enum()` | generic shell command-table enumeration (first user: the Command Bridge; any L1 module may enumerate) |
| `user_process_kill_by_tid()` | the unified-reaper kill path as a generic L1 API (fds + address space reclaimed, parent woken); first user: the Processes app |
| `tui_dt_*` registry | dynamic desktop menu tree (add/remove/find/depth/reset/count) |
| `tui_dt_guard/panic` | page crash containment |
| `tui_dt_page_*` helpers | full-screen page lifecycle shared by all desktop applications (open/rows/cols/flush/window/wait_close) |
| `tui_dt_console_wait_key()` / `tui_dt_reenter()` | parking helpers for the SHELL/TERMINAL/EDITOR surfaces |
| `tui_dt_toast*` / `tui_dt_loading_*` | result and progress feedback |
| `tui_dt_str_append` | bounded string append for dynamic labels |

The fix4 `tui_ext.h` seven interface sets (menu / page / widget /
command / theme / hotkey / event) remain the extension surface for
non-desktop TUI extensions; the desktop registry is the surface
for desktop applications.

## Verification

- build: 0 errors / 0 warnings with `-Werror` (gcc 14, `-ffreestanding`).
- `tui-test`: **90/90 checks all green** (BIOS + UEFI), including
  the new desktop-registry block (bootstrap fills the registry,
  files/procs/editor/terminal kinds and runner wiring, root depth,
  privilege-payload ELF header) and the fix4 7-interface blocks.
- regression suite: **28/28** (BIOS).
- chinese E2E: **21/21 BIOS + 21/21 UEFI** - the three former
  `tui-cn` launcher checks now drive the same pad through the
  desktop (`tui` -> quick-open `7` -> compose ni -> commit -> Esc),
  the command-count assertion moved 225 -> 224 with the removal.
- TUI E2E: **37/37 BIOS + 37/37 UEFI** (the fix1 re-enter round now
  exercises the desktop root: render, Esc exit, second session).
- interactive evidence (framebuffer, 800x600): desktop root en/zh
  with CJK glyph labels, Editor form -> nano -> save ->
  `cat /tmp/ed1.txt` returns the typed content, Terminal ->
  ush banner -> `echo` -> `exit` -> bridge message -> desktop ->
  F10 -> shell, digit `7` quick-opens the pinyin pad.
- `priv_test` containment: BIOS + UEFI (above).

## Driver adaptations (honest surface changes)

- `tools/wp10_chinese_e2e.py`: the removed `tui-cn` launcher checks
  now target the desktop IME pad; command count 225 -> 224; the
  `tui.backend=serial` config line is set before every desktop
  flow (otherwise the desktop renders to the invisible framebuffer).
- `tools/wp10_tui_e2e.py`: the fix1 re-enter round asserts the
  desktop root markers (Control Panel / Terminal / Command Bridge)
  and exits with Esc instead of the old main-menu Exit item.
- `tools/run_fix4_regression.py`: command count 225 -> 224.
