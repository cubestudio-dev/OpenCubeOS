<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
# 3.0.0-WP04: the L1 migration record

This work package completes the 3.0.0 restructure: every command of
the v2.2.0 baseline now runs from ring 3.  The L0 side stays a pure
service kernel - drivers, services and the numbered L1SVC ABI - and
the user plane is the embedded program registry: **one command = one
app = one folder** (`src/L1/<name>/main.c`), discovered mechanically
by `tools/gen_userprogs.py` into the `OC_USERPROGS[]` registry.

## Where the 251 v2.2.0 commands went

| Batch | Content | Count |
|-------|---------|-------|
| G3-G5 | 140 SVC wrappers + 31 file tools + the nano/vi editor shells | 171 |
| G6 | health / dmesg / logcat / logctl / watchdog | 5 |
| G7 | the l1 family: run, l1test, ldd, help (+ help_*_test invariants) | 7 |
| G8 | vitals 5 (mem/frag/heap/sched/syncstat) + crash 3 | 8 |
| G9 | 18 ring-0 self-tests (diag ops 9-26) | 18 |
| G10 | 13 chipset diagnostics (diag ops 27-39) | 13 |
| G11a | ssh + sshd (the RFC 4251-4254 engine pair, revived verbatim) | 2 |
| G11b | ime + ime-test (pinyin engine, DISP ops 11/12 revived) | 2 |
| G11c | the nine full-screen TUI pages (category "tui", 30 files ~13k lines) | 9 |
| stays in ush | shell-state builtins (cd/export/alias/...) - a subprocess cannot mutate the shell's own process state | 9 |
| subcommands | `health export` / `health watch` live inside the health app | 2 |

Total migrated: **235 of 251**; the remaining rows are the documented
shell-state/`subcommand` classes above (plus one audit false-positive
and the dual-homed counting caliber).

## Parity proof

The G13 acceptance matrix ran a 34-command battery against a real
v2.2.0 build (same toolchain) side by side in QEMU - structure is
line-for-line identical; only volatile state (tick counts, free
bytes, heap block counts) and version strings differ.  Per-batch
evidence (verbatim transcripts, interactive sendkey sessions, VNC
page screenshots, the VLM-read desktop menu tree, SSH e2e with
paramiko) lives under `evidence/3_0_0_wp04_*`.

## New ABI surface (append-only)

- category **19 "diag"** - 39 operations (vitals, crash, self-tests,
  chipset)
- category **20 "tui"** - 9 operations (the desktop + classic pages)
- net ops **13/14** (ssh/sshd), display ops **11/12** (ime/ime-test,
  back on their v2.2.0 numbers)
- `tools/gen_l1svc_ops.py` mirrors the numbering to the ring-3 side

## Engineering notes worth keeping

- **Identity-window discipline**: the kernel image must stay below
  the 0x400000 boundary (P3-1 security invariant).  The GB2312 font
  tables (~292 KiB) and the 274 embedded programs (515 KiB of ELF
  payloads) both moved behind per-object gzip containers with lazy
  inflate into kernel-heap caches - the image shrank while the TUI
  came back.
- **Deterministic builds**: two clean builds minutes apart are
  byte-identical (gzip `mtime=0`, xorriso date/GUID pinning, staged
  tree epoch normalization, FAT date scrubbing, and a post-pass that
  zeroes the 593 Rock Ridge TF ctime stamps xorriso hardcodes to the
  assembly moment).
- **Interactive ops bypass the text capture**: full-screen pages
  (tui), session servers (sshd) and the live IME compose line render
  live on the console/serial; only plain-text ops return through the
  4096-byte SVC result buffer.

Live bugs found and fixed along the way are recorded in the per-batch
commit messages and the evidence folders (F6 IRQ-context interrupt
enablement, F7 stale RSDP pointer into loader memory, F8 a health
format that had drifted from its v2.2.0 source).
