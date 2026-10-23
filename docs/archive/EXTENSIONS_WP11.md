# Open Cube OS - WP-11-crash_recovery Extension Interfaces

SPDX-License-Identifier: Apache-2.0
Copyright 2026 cubestudio-dev <cubestudio@qq.com>

WP-11 adds a complete crash-recovery layer: unified panic capture with
register snapshots and call tracebacks, persistent core dumps, a
crash-log ring with on-disk persistence, a post-panic policy (safe
halt or reboot), kernel-stack guard pages, ring-3 SIGSEGV delivery,
and TUI sub-page crash containment.  Every interface below has a
header declaration with comments, a working implementation, a
documented error contract, and a QEMU-verified test (the
`tools/wp11_crash_e2e.py` gate runs all 14 scenarios end to end).

## Files

| file                              | purpose                                        |
|-----------------------------------|------------------------------------------------|
| kernel/core/core_crash.h/.c       | panic path, core dump, crashlog, policy, WDT   |
| shell/shell_cmds_crash.c/.h       | crashlog / crashtest / dumpinfo commands       |
| kernel/arch/x86_64/arch_exceptions.c | dispatcher wiring: TUI fence, SIGSEGV, #DF   |
| kernel/core/core_usermode.c       | user_signal_deliver (shared signal primitive)  |
| kernel/core/core_sched.c          | guard pages + switch-time stack bounds check   |
| linker.ld                         | __tui_text_start/__tui_text_end code window    |
| userprogs/sigsegv_test.asm        | ring-3 fault -> SIGSEGV catch test             |
| userprogs/stackovf.asm            | ring-3 recursion overflow kill test            |
| tui/tui_dt_apps.c                 | TUI crash-test page (guard verification)       |
| tools/wp11_crash_e2e.py           | 14-scenario acceptance gate                    |
| tools/wp11_tui_isolation_e2e.py   | TUI containment E2E (S14)                      |

## Panic capture

```c
/* panic handler chain (L1 observers, most-recently-registered first) */
typedef int (*kernel_panic_handler_fn)(const char *reason,
                                       const arch_irq_frame_t *frame);
int  kernel_panic_register_handler(kernel_panic_handler_fn fn);
int  kernel_panic_unregister_handler(kernel_panic_handler_fn fn);

void kernel_panic(const char *reason);
void kernel_panic_frame(const char *reason, const arch_irq_frame_t *f);
```

`kernel_panic()` is the no-frame entry (assertions, watchdog,
`crashtest panic`); `kernel_panic_frame()` is the exception-dispatcher
entry and prints the exact exception frame.  Both print the banner +
16-GPR register snapshot + call traceback (rbp chain, or a labelled
approximate stack scan for this O2 kernel) on the serial console and
in red on the VGA console, save the core dump, flush the crash log,
run the registered handler chain, and apply the policy.  A handler
returning non-zero claims recovery: the panic path returns and the
dispatcher resumes the (repaired) frame.  `crash_contained()` is the
non-fatal variant used by the TUI fence: it records + notifies and
returns, so the caller can longjmp to its own recovery point.

`OC_ASSERT(cond)` panics with `"assertion failed: <cond>"`.

## Recovery policy

```c
#define CRASH_POLICY_HALT    0     /* default: cli + hlt safe loop   */
#define CRASH_POLICY_REBOOT  1     /* 8042 pulse + ACPI 0xCF9 reset  */

int  crash_recovery_set_policy(int policy);   /* 0 ok, -1 bad value  */
int  crash_recovery_get_policy(void);
const char *crash_recovery_policy_name(int policy);
```

Shell: `crashtest policy [halt|reboot]`.  The reboot path is the
production WP-10u `core_power_reboot()` (block-cache flush + 8042 +
0xCF9 fallback) - QEMU-verified end to end: panic -> reset -> second
boot banner (E2E S13).

## Core dump

```c
int  core_dump_save(const char *reason, const arch_irq_frame_t *f);
int  core_dump_read(void *buf, u32 cap, u32 *out_total);
u32  core_dump_size(void);
int  core_dump_persisted(void);
void core_dump_alloc(void);          /* kmain-time buffer reservation */
```

Image layout: `oc_dump_hdr_t` (magic `OCDUMP01`, build, version,
reason, task, CR3, walked PTE count, stack-window bounds) followed by
the 22-u64 register block (exception frame, or a live snapshot in the
same layout) and the kernel-stack window copy.  The buffer is
kmalloc'd once at boot - the panic path never allocates.  Persisted
via VFS to `/var/crash/core.dump` (ramfs rootfs or a mounted FAT32,
whichever backs the path); with no filesystem the first
`OC_DUMP_SERIAL_MAX` (2048) bytes are hex-dumped on the serial console
so the data survives.  `dumpinfo` decodes the header back through
`core_dump_read()`.

Boundary (stated honestly, see KNOWN_ISSUES): the #DF (double-fault)
path skips the dump - by the time a #DF fires the faulting stack is
destroyed and the deep VFS/heap call chains re-faulted there during
bring-up.  Every other panic source (panic/exc/pf/assert/wd) saves +
persists the dump; E2E S6/S7 verify the round trip.

## Crash log

```c
#define OC_CRASHLOG_CAP 32
void crashlog_record(u32 vector, u64 error_code, u64 rip, u64 rsp,
                     u64 cr2, const char *task_name, i32 tid, i32 pid,
                     u32 ring);
int  crashlog_count(void);
const crashlog_entry_t *crashlog_get(int index);
int  crashlog_flush(void);            /* -> /var/log/crash.log       */
void crashlog_boot_alloc(void);
```

Ring of the last 32 faults.  Sources: CPU exceptions 0..31
(ring-0 = panic, ring-3 = user-kill or SIGSEGV), manual panic (32),
watchdog timeout (33), assertion (34), contained/TUI (35), kernel
stack overflow (36).  Entries carry jiffies + RTC epoch, vector, ring,
error code, rip/rsp/cr2, task name, tid/pid.  Recording is
nested-write-guarded and uses a boot-reserved kmalloc block - safe
from any fault context.  `crashlog` lists entries (name unchanged
since WP-04); `crashlog flush` appends the ring to
`/var/log/crash.log` (also done automatically on every panic);
`cat /var/log/crash.log` reads it back (E2E S5: 3 entries -> 467
bytes -> read back).

## Soft watchdog

```c
void crash_wdt_init(void);            /* 1 s tick, boot-DISARMED     */
void crash_wdt_kick(void);            /* shell heartbeat             */
void crash_wdt_blackout(int on);
int  crash_wdt_armed(void);
u64  crash_wdt_ms_since_kick(void);
void crash_wdt_force_timeout(void);   /* crashtest wd test hook      */
```

The tick is registered at boot but stays DISARMED (long synchronous
commands own the shell task and cannot kick - arming by default would
false-panic under exactly the regression-gate workloads; the decision
is documented in the source).  `crashtest wd` arms + rewinds the
heartbeat so the NEXT REAL tick raises the panic - the full
tick -> check -> panic -> dump -> policy chain runs in production code
(E2E S11).

## Kernel-stack overflow detection

- `core_kthread_create()` unmaps the LOWEST page of every kthread
  stack from the kernel address space (guard page); both free paths
  re-map it before PMM free.
- `core_sched_switch_to()` bounds-checks the next task's saved rsp
  against its stack window before the context switch loads it.
- The exception dispatcher attributes a #PF whose cr2 lands in a
  guard page, or a #DF with a burned/garbage rsp, as
  `KERNEL STACK OVERFLOW (..., task=<name>)`.
- `crashtest ovf` spawns a kthread whose 4-KiB-frame recursion (indirect
  call + post-call volatile write so no tail-call optimization) burns
  to the guard page: guard #PF -> #DF -> attributed panic -> HALT
  (E2E S12, 3/3 stable runs).
- Ring-3 side: `run stackovf` (fork + unbounded recursion) is killed
  at the 64-KiB stack boundary and the parent wakes (E2E S3).

## Ring-3 SIGSEGV

```c
int user_signal_deliver(user_proc_t *proc, struct arch_irq_frame *f,
                        int sig);
```

The P3-12 delivery primitive (22-u64 frame save + handler entry +
BUG-0277 restorer trampoline), extracted so the exception path and the
syscall-return path share EXACT semantics.  A faulting user process
with a registered SIGSEGV handler gets the signal delivered first
(iretq straight into the handler); a 3-delivery ping-pong guard
(`sigsegv_delivered`, reset on exec) stops a handler that sigreturns
onto the same faulting instruction - the 4th fault kills, matching
POSIX force-kill semantics.  `run sigsegv_test` shows delivery #1 +
clean handler exit (E2E S2); `run badapp` (no handler) is killed as
before (E2E S4).

## TUI crash containment

`linker.ld` exports `__tui_text_start/__tui_text_end` around the TUI
objects.  On a ring-0 exception whose rip lies inside that window with
an active `tui_dt_guard()`, the dispatcher records the fault
(`crash_contained`) and calls `tui_dt_panic()`, which longjmps back to
the guarded runner: the desktop shows the "contained" toast and
survives.  The guard's longjmp return re-enables interrupts (the
exception entry cleared IF; no iretq will ever restore it).  The
Control Panel -> Crash test page raises a real ring-0 fault in TUI
code on demand (E2E S14: CONTAINED notice, no panic, F10 exits to the
shell, crashlog records the fault).

## Shell commands

| command     | purpose                                              |
|-------------|------------------------------------------------------|
| `crashlog`  | list the ring (WP-04 name kept); `crashlog flush`    |
| `crashtest` | panic/exc/pf/assert/wd/ovf/dump/policy [halt/reboot] |
| `dumpinfo`  | dump image status + header decode + policy + WDT     |

Command count: 224 -> 226.  `help` three-mode output unchanged.

## Error contract quick reference

| call                          | success          | failure                    |
|-------------------------------|------------------|----------------------------|
| crash_recovery_set_policy     | 0                | -1 (bad policy)            |
| kernel_panic_register_handler | 0                | -1 (NULL) / -2 (chain full)|
| core_dump_save                | bytes captured   | -1 (no boot buffer)        |
| core_dump_read                | bytes copied     | 0 (empty / cap 0)          |
| crashlog_flush                | bytes written    | -1 (no fs / write failed)  |
| crashlog_get                  | entry pointer    | NULL (out of range)        |
| user_signal_deliver           | 0                | -1 (no handler / bad sig)  |
