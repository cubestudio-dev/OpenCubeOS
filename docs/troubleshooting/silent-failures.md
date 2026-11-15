<!-- SPDX-License-Identifier: Apache-2.0 -->
# Silent failures — when nothing prints

Most of Open Cube OS fails loudly: an error string, a `FAIL` verdict,
a panic banner. This page is about the other class: things that go
wrong **without printing anything**, why the design made them quiet,
and the exact commands that surface them. Read this once; it will save
you an afternoon.

The one-line rule: **`health` first, `dmesg 100` second, `crashlog`
third.** Those three see through almost every silent failure.

---

## 1. Output capture — when your console shows nothing because the
text went into a buffer

### The mechanism

Most service ops are v2.2.0 shell-command bodies that print via
`screen_console_puts`. To serve them through the L1SVC syscall, the
dispatcher wraps them in `core_kcap_run_fn(fn, arg, out, cap)`:

- While a capture is active, the capture hook **IS the console hook**
  (the console driver holds exactly one hook slot).
- Characters go into the caller's buffer; both the framebuffer draw
  and the serial tee are **suppressed** for the duration.
- Captures stack up to 4 levels; the boot serial tee is reinstalled
  when the last capture ends.
- Overflow is "reported by marking, never by truncating silently".

Consequences you will actually meet:

- A command run from a wizard or a script prints **nothing live** —
  its output went to the capture buffer and is rendered by the caller.
  This is correct, not a failure.
- If a captured body loops or waits for console input, it looks like a
  dead machine: the prompt never comes back.

### The three routing styles (why some ops print live and others do not)

| Style | Ops | Why |
|-------|-----|-----|
| **captured** (`core_kcap_run_fn`) | most one-shot text ops (status, listings, tests) | output belongs to the caller |
| **direct** (`out[0]=0; return fn(arg);`) | `sshd`, all 9 TUI ops, `ime -i`, `power sleep` | captured bodies must not sleep — running these captured swallowed live console output (found live during OCIRE-supervised boot) |
| **buffer-render** (service writes `out` itself) | l1, klog, health, diag, fs, time, watchdog dispatchers | no legacy print body involved |

So when `power sleep 2` prints its countdown live but `health` does
not echo anything until it returns — that is the routing table, not a
bug. And if you ever see **no output at all** from `sshd` or a TUI
page, check the serial console: direct-routed output prefers the live
console and can be missed on a slow terminal.

## 2. The return-value convention — no errno to read

There is **no global `errno` variable and no `errno.h`** (freestanding
kernel, no libc):

- 0 = success, negative = failure (`-1` generic).
- Precision comes from the 8 dense negative-enum families (61 codes —
  [error-codes.md](error-codes.md)).
- Ring-buffer APIs return slot indices ≥ 0; `-1` = past the window.
- Watchdog slot APIs return -1 (table full) / -2 (bad args).

The trap: a command that fails with `-1` and prints nothing (the
argument was legal but the operation was refused, e.g. writing to the
OCIRE-protected recovery disk) leaves **no message anywhere** unless
the service chose to klog it. When a command "does nothing":

1. Re-run it and watch `dmesg 30` in a second step — many refusals are
   klog WARN/ERROR lines, not console lines.
2. Check the family's `_test` stub — it exercises the same op with
   visible verdicts.
3. Remember the deliberate refusals: writes to the recovery disk
   (`blk: write ... REFUSED` is klog-only), `kill` of ocire_guard
   (looks like "no such process").

## 3. The "honest skip" pattern — a PASS-shaped non-answer

Hardware-absent conditions deliberately report `(honest skip)` /
`(honest FAIL)` instead of failing the suite. Examples:

- `hpet: legacy route not available (honest skip)`
- `oneshot: arm failed (honest FAIL)`
- `[audio_rw_test] no sound cards registered => SKIPPED`
- `msitest`'s `e1000: no MSI cap or BAR0  SKIP (reported honestly)`
- `[nic_rw_test] no NICs registered => SKIP`
- `wifi scan` in QEMU: the honest "no radio" output

Reading rule: **a suite that ends in honest skips tested nothing**.
If you expected the hardware to be there (you attached the device,
you passed the QEMU flag), an honest skip means the probe did not find
it — go check the bus (`lspci`, `usb`), not the test.

## 4. Health first — the three alarm bits

The health monitor samples every second and raises exactly three
alarms (klog WARN):

```
alarm: memory >90% used (<used>/<total> KiB)
alarm: <n> active tasks (>64)
alarm: CPU >=90% busy for 10 consecutive seconds
```

Before deep-diving any "slow" or "weird" machine, run:

```
ush> health              # one snapshot: cpu/mem/heap/tasks/disk/net
ush> health watch        # live monitor until ENTER
ush> health export       # write the report to a file
```

The alarms also feed watchdog slot 0 ("system-heartbeat") as an extra
feed source: an alive-but-alarmed system keeps feeding the dog; a dead
one stops, and the WP-11 panic path (dump + crashlog + policy) fires.
So a machine that "silently" rebooted was probably alarmed first —
`health` on the next boot plus `/var/log/kernel.log` tells the story.

## 5. Log-level filtering — the message you never saw

klog has 5 levels (DEBUG 0 … FATAL 4) with per-level emission
switches, a live filter, and a global level. If a line never appears:

- The **level is off**: `logctl status` shows the live level and the
  per-level mask; `logctl level -1` is fully muted. Re-enable with
  `logctl enable debug` (or the level you need).
- The **filter** ate it: `logctl filter` settings apply to live
  channels AND dmesg/logcat replays. `logctl filter clear` resets.
- The **ring wrapped**: 1024 entries, overwrites counted — the dmesg
  footer `-- <shown> lines (ring <n>/1024, dropped <d>) --` shows
  drops. `dmesg 1024` for the whole window.
- You looked at the wrong channel: `logctl flush` writes
  `/var/log/kernel.log` (rotate keeps 3: kernel.log.1 ...).

Quick un-hide:

```
ush> logctl level 0           # everything
ush> logctl test              # emits one line per level (module klogtest)
ush> logcat -l debug -f       # follow everything live; ENTER exits
```

`logctl test` is the calibration tool: if its 5 lines appear, the
pipeline works and your target message genuinely was not emitted.

## 6. Crashlog — the 32-slot history you forgot existed

`crashlog` records every panic into a 32-entry ring (fields include
vector, rip, rsp, cr2, tid/pid, task name), flushed to
`/var/log/crash.log`; core dumps go to `/var/crash/core.dump`. After
any unexplained reset:

```
ush> crashlog          # were there panics you never saw?
ush> dumpinfo          # core dump buffer status
ush> dmesg 100         # this boot's klog
ush> cat /var/log/crash.log
```

Empty output prints `no dump captured yet (trigger: crashtest panic|exc|pf)` — which is also how you verify the machinery on a
healthy machine. A #DF panic is deliberately minimal (one static
serial line, no record/dump/flush — stated in KNOWN_ISSUES): if the
machine died with a one-line banner, check **serial**, not crashlog.

## 7. OTA that silently does not update — the six-cause chain

`update`/`checkupdate` failing quietly is the classic silent failure
because most of the chain fails inside codes, not console text. Walk
the six causes in order:

| # | Cause | How to see it | Fix |
|---|-------|---------------|-----|
| 1 | `update_url` wrong (bad scheme/typo) | `config get update_url` | `config set update_url http://host/path` (must be http:// or https://) |
| 2 | DNS cannot resolve the update host | `dns <host>` → `DNS resolution failed` | fix DNS server (re-run `dhcp`, or set `ip <a> <m> <g>` and check the DNS field) |
| 3 | No A/B disk | `update status` lacks `A/B disk: present` | `lsblk` → `abdisk <dev>` (destructive; wizard-guarded) |
| 4 | Signature/verification failed | re-run `update` — the `<why>` is `SHA256 mismatch` / `package (tar.gz) invalid` | re-download; verify the server's package_sha256 |
| 5 | Version comparison says up-to-date | `checkupdate` prints `Current version is up to date.` | not a failure — the feed's version field must exceed yours; check the server's update.json |
| 6 | Network fully down | `ip` shows 0.0.0.0; `ping <gw>` fails | `dhcp` (or static `ip`), then re-check |

Two extra quiet paths: `online_update=no` in the config makes the
**autoupdate** background path skip silently (`[autoupdate] skipped: network not ready` is klog-only) — `config get online_update`; and
`auto_check=no` similarly disables the boot-time check.

The 5-step `update` wizard (no arguments) walks causes 1→3→6 in order
(IP probe → DHCP offer → A/B disk probe → DNS → check → install), so
when in doubt just run `update` with no arguments and read the `[n/5]`
step that fails.

## 8. Serial echo and the eaten `"e"` — tolerant reading

The serial console is both an input and an output channel; when the
guest echoes your keystroke while printing, the echo can overlap
in-flight output. The classic artifact: a bracketed tag like
`[ocse]` arrives as `[ega]` wait — exactly: the serial-echo overlap
**eats the leading `e` of `ega]`-shaped fragments** (the tooling
calls it the `ega]` phenomenon). What you see on screen can be a
character-shifted version of what the guest printed.

Tolerant reading rules (the same ones the E2E tooling uses):

- Treat `[tag]` brackets as **optional** when grepping boot output —
  match on the inner words, not the punctuation.
- Strip the echoed command line before matching (the guest echoes what
  you typed, then prints the result).
- Expect ANSI CSI sequences interleaved; strip them before regex.
- Keystrokes a serial link cannot carry (arrows, Home/End) must go in
  via the QEMU monitor `sendkey`, not by typing escape codes — CSI
  bytes over a TCP socket arrive too late at boot-menu time.
- Known flake: QEMU intermittently drops serial RX bytes right after
  an HMP screendump (verified with an in-kernel RX trace) — never take
  screenshots in the middle of a functional round; re-run flapping
  checks isolated.

If a single weird character is your only symptom, re-run the command
before debugging the OS: it is usually the link, not the kernel.

## 9. A checklist for "nothing happened"

1. `health` — alarms? (memory/tasks/CPU)
2. `dmesg 100` — any WARN/ERROR from the family? (mind the level
   filter: `logctl status`)
3. `crashlog` + `dumpinfo` — did something die earlier?
4. Re-run the command and watch for the **return path**: silent `-1`
   usually means a deliberate refusal (protected disk, guard channel,
   disabled feature — see §2).
5. Was the output **captured** (§1)? Run the op from the bare command
   instead of the wizard, or vice versa.
6. OTA-shaped problem → the six-cause chain (§7).
7. Garbled single characters → serial echo (§8); re-run first.
8. Still nothing: capture serial (`-serial file:boot.log`) and check
   the klog ring for FATAL lines that never reached the screen —
   the serial console is authoritative.

## Related pages

- [error-codes.md](error-codes.md) — the codes behind quiet refusals
- [error-catalog.md](error-catalog.md) — the loud failures, by
  subsystem
- [../manual/hidden-commands.md](../manual/hidden-commands.md) — the
  stubs that make kernel paths print verdicts
- [qemu-debugging.md](qemu-debugging.md) — capture techniques
