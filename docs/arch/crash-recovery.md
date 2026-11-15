<!-- SPDX-License-Identifier: Apache-2.0 -->
# Crash recovery: panic, dump, crashlog, watchdogs, and the OCIRE takeover

When the kernel fails, four mechanisms fire in sequence - the panic
path captures the moment, the crashlog ring remembers history, the
soft watchdog catches silence, and the boot-state relay hands the
machine to OCIRE. Files: `ocos/services/crash/`
(`svc_crash_core.c` 1018 lines, `svc_crash_watchdog.c`),
`ocos/kernel/arch/x86_64/arch_exceptions.c` (the dispatcher side),
`core_boot_state.c` (the relay's kernel half). The layering, verbatim
from the core header:

```
 *   exception entry (arch_exc_dispatch, ring-0 fault)
 *        └─> kernel_panic_frame(reason, frame)      [this module]
 *              ├─ crashlog_record(...)              ring buffer entry
 *              ├─ panic banner + register snapshot  serial + VGA(red)
 *              ├─ stack traceback                   rbp chain + scan
 *              ├─ core_dump_save(...)               memory + /var/crash
 *              ├─ registered panic handlers         L1 observers
 *              ├─ crashlog_flush()                  /var/log/crash.log
 *              └─ policy: HALT (cli;hlt) or REBOOT (8042 + 0xCF9)
```

## The exception frame (the capture shape)

From `arch_idt.h` - 22 u64s, matching `OC_DUMP_REGS_U64 22`:

```c
typedef struct arch_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} arch_irq_frame_t;
```

## The panic banner and traceback

`kernel_panic(reason)` / `kernel_panic_frame(reason, frame)` are core
slots; implementations are installed at boot by the crash service via
`arch_set_panic_handlers` (+ `arch_set_crashlog_record`). Before that
install, the fallback prints `\nKERNEL PANIC (no crash handler installed): <reason>\nSystem halted.` and cli;hlt.

The real path (`svc_crash_core.c`):

1. **Nested-panic guard**: `*** NESTED PANIC INSIDE PANIC - HALTING ***` -> cli;hlt.
2. **CR3 normalization first**: `mem_vmm_switch_as(kernel_as)` so the
   traceback/dump/heap reads are safe even from a user address space.
3. `klog_record(KLOG_FATAL, ...)` - ring only, no VFS at depth.
4. The banner (serial authoritative, VGA red best-effort):
   `*** KERNEL PANIC: <reason> [task=<name>] ***`, `kernel: Open Cube OS <version> (build 0x<hex>)`, `source: exception <n> (<name>) err=0x<hex>`, then the register dump (4x4 grid r15..rax +
   rip/cs/ss/rflags/rsp/int/err + cr2 for #PF + cr3), the task line,
   and the **traceback** - an rbp-chain walk plus a stack scan
   filtered by `looks_like_code`.
5. Stack-exhaustion annotation when #DF rsp < stack_base:
   `[KERNEL STACK OVERFLOW: exhausted, task=<name>]`.
6. Core dump save, crashlog flush, registered panic handlers (up to 4,
   most-recently-registered first; a handler returning non-zero claims
   recovery: `*** panic handler claimed recovery - resuming ***`).
7. **Policy**: HALT (default, cli + safe-hlt loop) or REBOOT (8042 +
   0xCF9) - `crashtest policy [halt|reboot]` switches it live.

The **#DF special case** is deliberately minimal: handled entirely at
dispatcher depth with one static 192-byte serial line buffer via
`arch_exc_ser_puts` - "No record, no dump, no flush for #DF (stated in
KNOWN_ISSUES)". Banner: `source: #DF Double Fault err=0x.. rip=0x.. [h]handled at dispatcher depth` (the typo is verbatim in source).
`crash_contained()` is the non-fatal variant: record + serial notice,
return (the TUI-guard mechanism).

`OC_ASSERT(cond)` -> `kernel_panic("assertion failed: " #cond)`.
`crashtest panic|exc|pf|assert|wd|ovf|dump|policy` injects every one
of these paths on demand (`exc` executes `ud2`; `pf` writes to
0x700000000000 - PML4[224], guaranteed not-present, because a NULL
deref in C would be UB-folded to ud2 AND page 0 is identity-mapped).

## The core dump (OCDUMP01)

```c
#define OC_DUMP_STACK_BYTES   4096    /* kernel-stack window copy     */
#define OC_DUMP_SERIAL_MAX    2048    /* hex-print cap when diskless  */
#define OC_DUMP_REGS_U64      22      /* arch_irq_frame_t, in u64s    */

typedef struct {
    char magic[8];            /* "OCDUMP01"                          */
    u32 hdr_len;              /* sizeof(oc_dump_hdr_t)               */
    u32 image_len;            /* total bytes captured                */
    u64 build;                /* OC_KERNEL_BUILD                     */
    char version[24];         /* OC_RELEASE_VERSION                  */
    char reason[40];          /* short panic reason                  */
    u64 jiffies;
    u64 rtc_epoch;
    u64 cr3;                  /* page-table root at capture time     */
    u64 pte_count;            /* present leaf PTEs walked            */
    u32 tid, pid;
    u32 ring;
    char task[16];
    u64 stack_base, stack_size, stack_rsp;   /* kernel-stack window  */
    u32 stack_copy_len;       /* bytes actually copied               */
    u32 regs_present;         /* 1 = frame captured, 0 = live regs   */
} oc_dump_hdr_t;
```

Image layout in capture order: header, regs (22 u64 or live snapshot),
task block, stack window (up to 4 KiB clipped to the task's stack),
tables (CR3 + a PML4 self-scan PTE count). Persisted to
`/var/crash/core.dump` - works on ramfs rootfs AND FAT32 mounts; if NO
filesystem is up, the first 2048 bytes are hex-dumped to serial so the
data is not lost. The buffer is allocated at boot by `core_dump_alloc()`
- kmalloc, NOT .bss, **once, up-front**: "after this point kmalloc may
fail or the heap may corrupt, but the panic path only ever touches the
already-reserved buffer" ([memory.md](memory.md)).

## The crashlog ring

```c
#define OC_CRASHLOG_CAP        32
#define OC_CRASH_SRC_PANIC     32
#define OC_CRASH_SRC_WDT       33
#define OC_CRASH_SRC_ASSERT    34
#define OC_CRASH_SRC_CONTAINED 35
#define OC_CRASH_SRC_STACKOVF  36

typedef struct {
    u64 jiffies;         /* core_timer_ticks() at record time        */
    u64 rtc_epoch;       /* seconds since 1970 (0 = rtc unavailable) */
    u32 vector;          /* 0..31 exception, 32..36 synthetic source */
    u32 ring;            /* 0 = kernel, 3 = user                     */
    u64 error_code;
    u64 rip;
    u64 rsp;
    u64 cr2;             /* #PF faulting address, 0 otherwise        */
    i32 tid;
    i32 pid;
    char task[16];       /* task name (or "user:<pid>")              */
} crashlog_entry_t;
```

32 entries; **every** exception landing anywhere in the system (user
kills included, via the dispatcher's crashlog record hook) appends
one. `crashlog` (L1SVC diag op 6) prints each entry;
`crashlog flush` appends the ring to `/var/log/crash.log`; empty ring:
`"crashlog: no exceptions recorded"`.

## Watchdogs: soft, slotted, and hardware

**Soft watchdog** (`crash_wdt`): `OC_WDT_TIMEOUT_MS 15000` - a periodic
core_timer callback checks a heartbeat stamp; the shell line editor
kicks it on every key; 15 s of armed silence panics with source
`OC_CRASH_SRC_WDT`. Starts DISARMED (long synchronous commands own the
shell task and cannot kick).

**Unified slot engine** (spec 5.2): up to 8 monitored slots, 1 s tick
checks every ARMED slot, four policies:

```c
#define WD_POLICY_PANIC  0   /* kernel_panic("watchdog timeout: ...") - full WP-11
                               crash path (dump + log + policy) */
#define WD_POLICY_REBOOT 1   /* klog ERROR + core_power_reboot() directly */
#define WD_POLICY_KILL   2   /* klog ERROR + core_sched_task_exited(tid) - the
                               monitored task is killed, kernel lives */
#define WD_POLICY_LOG    3   /* klog ERROR once per timeout episode (no action;
                               re-arms on the next feed) */

#define WD_MAX_SLOTS      8
#define WD_HW_TIMEOUT_MIN 2      /* TCO/WDAT seconds floor           */
#define WD_HW_TIMEOUT_MAX 63     /* 6-bit TCO timer field            */
```

Slot 0 = "system-heartbeat" (registered by `watchdog_init()`, wraps
the WP-11 shell-heartbeat semantics, default timeout 15 s, policy
PANIC; `watchdog_feed(0)` is the L1-visible kick). The `watchdog`
command surfaces everything: `status`, `slots`, `hw start|stop|feed| timeout <2..63>`, per-slot `start|stop|feed|timeout|register`, and
`test kill|panic|log` (spawns a `wd-starve` kthread that registers
itself for 3 s and never feeds - "expect the policy in 3 s").

**Hardware**: probed at boot (ACPI WDAT table first, then Intel TCO
on the LPC bridge - present on QEMU q35, absent on default pc, "the
probe honestly reports 'none'") but **never armed at boot**; the
software check runs first on every tick and when both are armed the
hardware timeout is set >= 2x the software one. The ring-3 face is
`watchdog` (cat 11, 1 op carrying the whole subcommand line).

**Health linkage**: alarms feed slot 0 as an extra feed source - an
alarmed-but-alive system keeps feeding the dog, a dead one stops and
the panic path fires ([../internals/health.md](../internals/health.md)).

## The OCIRE takeover chain

When panic policy is REBOOT - or when the kernel hangs so hard that
the 6300ESB watchdog OCBL armed fires - the recovery chain is:

1. Reset. The 0x98000/0x9B000 islands survive (E820-reserved,
   reset-surviving by measurement).
2. OCBL's statemon boot check reads them: stage stuck below RUNTIME +
   `>= 8 s` RTC silence -> judged BOOT_FAILED; OCBL writes the
   post-mortem record (`ocst_record_t`: kind, stage, silence seconds,
   seq, timestamp, ones-complement checksum), shows a 4-second notice,
   pulses the 8042 reset.
3. Next boot, `ocbl_statemon_route_pending()` auto-selects the OCIRE
   entry **one-shot** (the `routed` bit makes auto-recovery strictly
   single-shot - a loop of failures lands the user in the recovery
   shell, not in a reset storm).
4. OCIRE explains itself: `ocire_ocbl_report()` reads the record and
   prints WHY - USER_REQUEST ("manual recovery session") or BOOT_FAILED
   with the stage name, silence seconds, and per-stage repair advice
   (RESET/KM_AIN/SCHED -> `syscheck`; DRIVERS -> `syscheck` + review
   drivers; SERVICES -> `repair`; L1_READY/runtime -> `logread` +
   `repair`).
5. The OCIRE shell's `syscheck` runs a **ten-point** integrity audit
   (disks, recovery volume, system volume, mailbox, log ring, agent
   digest vs mailbox SHA-256, golden manifest, golden files, system
   files, kernel copy); `repair quick` fixes metadata + restores
   differing files; `repair full` destructively reformats the system
   volume from the golden image (typed `YES` required); `return`
   clears the mailbox and reboots normally.

Full byte layout of the state islands: `include/ocbl_state.h`
([hidden-mechanisms.md](hidden-mechanisms.md)); the supervised-boot
mailbox dance: [system-map.md](system-map.md); user-facing recovery:
[../manual/recovery.md](../manual/recovery.md); dump format internals:
[../internals/crash-dump.md](../internals/crash-dump.md),
[../internals/watchdog.md](../internals/watchdog.md).
