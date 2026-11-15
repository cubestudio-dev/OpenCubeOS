<!-- SPDX-License-Identifier: Apache-2.0 -->
# Debug handbook

The complete field guide to extracting evidence from an OCOS
machine: serial capture, the kernel log ring, crash dumps and
register snapshots, and - most importantly - a methodology for the
times nothing is obviously wrong. Companion pages:
[debug-interfaces.md](debug-interfaces.md) inventories the ~93
test-stub programs; [qemu-testing.md](qemu-testing.md) covers
QEMU-specific behaviors; [debugging.md](debugging.md) is the short
entry point.

## 1. Serial extraction

Every boot line, panic banner and captured op mirrors to COM1
(115200 8N1, polled). Serial is the *authoritative* channel (VGA
is best-effort).

**QEMU**:

```
make run-bios          # -display none -serial mon:stdio -snapshot
# or explicitly:
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 \
    -display none -serial mon:stdio          # interactive (monitor on Ctrl-A C)
qemu-system-x86_64 ... -serial file:boot.log # capture to a file
qemu-system-x86_64 ... -serial stdio -d int,cpu_reset -D qemu.log
```

The pexpect harnesses use `-serial mon:stdio` with
`encoding="latin-1"` so every byte survives; the prompt regex is
`(?:ush>|oc>)\s` (override with `OC_PROMPT`).

**Real hardware**: a USB-TTL adapter on the COM1 header. The
serial tee encoding follows the `serial_encoding` setting
(`utf8|gbk`, display op 17) - set `gbk` when your terminal is a
GB2312-era console.

## 2. Reading the kernel log ring

The 1024-entry ring (24-entry bootstrap ring before `klog_grow()`)
records EVERYTHING unconditionally; the live channels (serial +
console) are gated by the live level (default INFO). The replay
ops apply the active filter to what they show.

Line format (identical in ring replay, `/var/log/kernel.log`, and
live channels):

```
[   12.345] WARN  net: link up (e1000)
[SSS.mmm]    LEVEL module: message
```

- timestamp = seconds.milliseconds since `klog_init`
  (`core_timer_now_ms`; 0.000 pre-PIT),
- level is one of DEBUG 0 / INFO 1 / WARN 2 / ERROR 3 / FATAL 4
  (rendered `DEBUG/INFO/WARN /ERROR/FATAL` in the columns),
- module is the 12-char tag (`net`, `health`, `ocire`,
  `klogtest`, ...).

Usage table (apps `dmesg`, `logcat`, `logctl`):

| Command | Grammar | What it does |
|---|---|---|
| `dmesg` | `dmesg [N]` | replay the ring tail (default 30, 1..1024 lines) through the active filter; footer `-- <shown> lines (ring <count>/1024, dropped <d>) --` |
| `logcat` | `logcat -l <lvl> -m <mod> -k <key> -t <sec> -n <N> [-f]` | filtered replay; `-l` debug/info/warn/error/fatal or 0-4, `-m` module exact, `-k` keyword substring, `-t` seconds cutoff, `-n` count; `-f` follows live at a 10 Hz poll (ENTER exits) |
| `logctl status` | | ring count, dropped, live level + per-level mask, file bytes/rotate info, filter state |
| `logctl level [-1..4]` | | set the live level (-1 mutes live channels; ring still records) |
| `logctl enable\|disable <level>` | | per-level emission switches |
| `logctl clear` | | reset the ring |
| `logctl flush` | | append unsynced entries to `/var/log/kernel.log` |
| `logctl rotate` | | kernel.log -> kernel.log.1 (keep 3) |
| `logctl threshold <512..16777216>` | | rotation threshold (default 1048576) |
| `logctl export [path]` | | dump to `/var/log/kernel-export.log` |
| `logctl filter clear \| [level] [module] [keyword]` | | set/clear the active filter ('-' = any); applies to live channels AND dmesg/logcat replays |
| `logctl test` | | emit 5 lines, one per level, module `klogtest` - the filter verification tool |

The on-disk channel (`/var/log/kernel.log`, 1 MiB rotation x3) is
an emit hook installed by `klog_on_mount()` after the first fs
mounts; it appends every not-yet-synced entry and auto-rotates.
IRQ-context writers skip the FILE channel (deferred to the next
task-context writer/flush/timer kick).

## 3. Crash evidence

### 3.1 The panic banner (what to send in a report)

```
*** KERNEL PANIC: <reason> [task=<name>] ***
kernel: Open Cube OS <version> (build 0x<hex>)
source: exception <n> (<name>) err=0x<hex>
<task line>
<register dump: 4x4 grid r15..rax + rip/cs/ss/rflags/rsp/int/err [+ cr2] + cr3>
<traceback: rbp chain + stack scan>
<policy: HALT or REBOOT>
```

Variants you may see: `[KERNEL STACK OVERFLOW: exhausted, task=<name>]` (stack-overflow attribution); `core dump: skipped in #DF mode` (#DF is handled minimally at dispatcher depth with one
static serial line - no record, no dump, no flush, stated in
KNOWN_ISSUES); `*** NESTED PANIC INSIDE PANIC - HALTING ***`;
`*** CONTAINED: <what> - isolated, kernel continues ***` (a panic
handler claimed recovery).

### 3.2 crashlog - the 32-slot ring

```
ush> crashlog          # each entry:
[i] <src-name> ring=N err=0x.. rip=0x.. rsp=0x.. [cr2=0x..] [@<rtc>s | @tick=N] [task=<name>] [pid=N]
ush> crashlog flush    # append all entries to /var/log/crash.log
```

Source names: the exception name (`#PF Page Fault`, ...) or a
synthetic source - PANIC 32 / WDT 33 / ASSERT 34 / CONTAINED 35 /
STACKOVF 36. Empty: `crashlog: no exceptions recorded`.

### 3.3 dumpinfo + the OCDUMP01 image

`dumpinfo` reports the core-dump buffer status. The saved
`/var/crash/core.dump` starts with:

```c
char magic[8];            /* "OCDUMP01" */
u32 hdr_len;              /* sizeof(oc_dump_hdr_t) */
u32 image_len;            /* total bytes captured */
u64 build;                /* OC_KERNEL_BUILD */
char version[24];         /* OC_RELEASE_VERSION */
char reason[40]; u64 jiffies; u64 rtc_epoch;
u64 cr3; u64 pte_count; u32 tid, pid; u32 ring; char task[16];
u64 stack_base, stack_size, stack_rsp; u32 stack_copy_len; u32 regs_present;
```

followed by regs (22 u64), task block, the 4 KiB stack window and
the page-table scan. Without any filesystem the first 2048 bytes
are hex-dumped to serial. `crashtest dump` captures one without
panicking - use it to validate your capture wiring before you need
it.

### 3.4 Reading the register snapshot

The dump's 22 u64s are `arch_irq_frame_t` (r15..rax, int_no,
error_code, rip, cs, rflags, rsp, ss). Per exception class:

| Exception | Field(s) that matter | Typical meaning |
|---|---|---|
| #PF (14) | `cr2` = faulting address; error_code bit0 (P) | P=0: not-present (NULL/wild pointer, or stack growth below RSP within limits); P=1: protection violation - never remapped, a real bug; U/S bit tells ring; I/D bit = instruction fetch (NX) |
| #GP (13) | error_code selector index | bad descriptor/privilege - classic ring-3 I/O attempt (IOPB denies all ports) |
| #DE (0), #OF (4) | rax/operands | divide/overflow at rip |
| #UD (6) | rip | executing garbage - check the traceback's code-range filter |
| #DF (8) | rsp vs task stack window | rsp at/below stack bottom => stack exhaustion attribution; #DF prints only the minimal line |
| #DB/NMI/#BP (1/2/3) | - | handled on IST2; kills of ring-3 origin run on the exit stack |
| watchdog (33) | task line | heartbeat starved 15 s while armed |
| stackovf (36) | task name | kthread guard page hit |

`rip` inside a task's guard page or below its stack base is the
smoking gun for stack overflow; the traceback prints the rbp chain
plus stack-scan candidates filtered by `looks_like_code`.

## 4. Silent-failure methodology

When there is no panic, no banner, no error line - work this
ladder top to bottom.

### 4.1 The health alarms (objective state)

`health` prints cpu/memory/tasks/disk/network plus the
`alarms:` line. The three bits, sampled every second, deduplicated
(one ERROR per crossing, re-armed when the metric recovers):

| Alarm | Trigger | Meaning |
|---|---|---|
| `MEMORY>90%` | `mem_used*100/mem_total > 90` | PMM+heap pressure - check `mem`, `frag`, `heap` |
| `TASKS>64` | active tasks > 64 | task-slot leak - `ps`/`sched`, look for unreaped exits |
| `CPU-LOCKED` | busy >= 90% for 10 consecutive samples | a spinner/pinned loop - `sched`, `nice` the suspect down |

### 4.2 The log ring (not the live console)

The ring records unconditionally even when the live level or a
filter hides lines: `logctl level 0` (open the live gate), then
`dmesg 200`. `logctl test` proves the pipeline itself works. If
the ring is empty at FATAL for a subsystem that "did nothing",
that subsystem may use the honest-skip convention (4.5) rather
than logging.

### 4.3 The kcap trap (output that vanished)

**The single most common silent-failure cause in this codebase.**
While an L1SVC capture is active, the capture hook IS the console
hook: characters collect into the caller's buffer and BOTH the
framebuffer and the serial tee are suppressed. Consequences:

- A captured op that internally triggers live output (a nested
  live session, a sleep-park, a klog FATAL that would have hit the
  serial) will look like the system went console-silent.
- Historical live-caught cases: `power sleep` captured installed
  the capture hook for the whole park and swallowed every console
  line during an OCIRE-supervised boot; `sshd` had to route direct
  so `net_sshd_log` reaches the console live.
- Rule: ops that park or own a live session route DIRECT
  (`out[0]=0; return fn(arg);`); only one-shot text renderers run
  captured. If you are adding an op and its output "disappears",
  check the routing style first ([writing-services.md](writing-services.md)).

### 4.4 Return-value conventions

- Syscalls return `>= 0` on success, negative on error (`-1` for
  most paths); `SYS_L1SVC` returns 0 for any *routed* call - the
  operation's own failure text is inside `out` (see
  [syscall-reference.md](syscall-reference.md) section 1.5/4).
- Driver/block/nic ops return 0 (or positive byte counts) on
  success and negative on error.
- If a wrapper prints `<name>: svc_call_failed`, the *category or
  op did not route* - a typo'd op number, a kernel-only service,
  or (cat 18) a non-guard caller refused:
  `ocire: refused (guard-only channel)`.

### 4.5 The "honest skip" mode

The tree deliberately distinguishes FAIL from SKIP. Recognize these
strings and do not chase them as bugs:

- `... => SKIPPED` (e.g. `[sample_rate_test] no sound cards registered => SKIPPED`) - precondition absent, test not run.
- `honest FAIL` (e.g. `oneshot: arm failed (honest FAIL)`) - the
  operation really was attempted and refused.
- `: SKIP - see the dedicated halt session` (halt/reboot tests -
  destructive, live halt deliberately not observed).
- `watchdog hw: none (no WDAT table, no Intel TCO LPC)` - the
  probe honestly reports absence (default `pc` machine has no TCO;
  use q35).
- `wifi scan` under QEMU prints the honest "no radio" output.
- `ocire: embed inactive (normal boot)` - cat 18 is dormant on a
  GRUB boot; that is correct behavior.
- `DesignWare: not present on this platform (honest skip)`.

### 4.6 Deadlock vs. hang

- The soft watchdog (15 s, slot 0 "system-heartbeat") panics with
  source WDT *only when armed and starved*; the shell line editor
  kicks it on every key - a machine that answers keystrokes is
  alive at the scheduler level.
- `ps`/`sched`: a task stuck in TASK_BLOCKED with no waiter is
  your candidate; `syncstat` shows primitive op counters.
- The boot-state monitor's `/HANGAT` injection (below) exercises
  the deliberate-hang path end to end.

## 5. Fault injection tools

### 5.1 crashtest - the 8 modes

```
usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]
  panic   : kernel_panic() direct
  exc     : ring-0 #UD exception
  pf      : ring-0 unmapped-address write fault
  assert  : OC_ASSERT(0)
  wd      : forced watchdog timeout
  ovf     : kernel stack overflow (guard page)
  dump    : capture a core dump without panicking
  policy  : show/set the after-panic policy
```

- `exc` executes `__asm__ volatile(".byte 0x0F, 0x0B\n")` (#UD).
- `pf` writes to 0x700000000000 (PML4[224], guaranteed
  not-present - a C NULL-deref would not work: UB folds to ud2 AND
  page 0 is identity-mapped).
- `panic|exc|pf|assert|wd|ovf` REALLY crash the kernel (v2.2.0
  behaviour); `policy`/`dump`/usage are safe. Unknown variant:
  `crashtest: unknown variant 'x' (try: crashtest help)`.

Use `crashtest dump` + `dumpinfo` + `crashlog` to prove your
evidence chain before a real failure; use the crashing modes to
test panic policy and the OCBL/OCIRE relay.

### 5.2 msitest - MSI injection

`msitest` (diag op 21) triggers real MSI/MSI-X interrupt delivery
(LAPIC vectors 0x30..0x6F) - use it to prove the MSI path
(enables, vector registration, EOI) independently of device
traffic.

### 5.3 /HANGAT - the boot-stage hang injector

Kernel cmdline `oc.hang=<stage>` halts the boot *after* the named
boot-state stage write + watchdog arm, exercising the real
6300ESB relay chain (stage monitor writes, heartbeats stop, the
hardware resets, OCBL judges, the record routes to OCIRE).
`oc.stmon=off` disables the whole state monitor. Stage names from
`ocst_stage_name`: BOOT, KM_AIN_ENTERED, SCHED_READY,
DRIVERS_READY, SERVICES_READY, L1_LAUNCHED, L1_READY.

### 5.4 watchdog test injectors

`watchdog test kill|panic|log` spawns a `wd-starve` kthread
(priority 15) that registers itself monitored for 3 s and never
feeds - "expect the policy in 3 s" (task killed / kernel panic /
ERROR log per the registered policy).

## 6. Host-side tools

- `build/opencube_base.elf` is the pre-`strip` symbol carrier
  (the pipeline links with `-g`, then strips the final ELF).
- gdb: `qemu ... -s -S`, then `target remote localhost:1234`,
  `break kmain`, `continue`.
- `-d int,cpu_reset,guest_errors -D qemu.log` logs interrupts,
  resets and errors.
- `make test` runs the host unit harnesses (crypto vectors, tar,
  gzip, MB2 parse, CUBIC, block-512 gate, ...) - much faster to
  iterate on than a boot loop; several harnesses use ASAN locally.
- Reproducible builds mean a crash reproduced twice is the same
  bytes - no heisen-builds across attempts
  (`make reproducible-check`).
