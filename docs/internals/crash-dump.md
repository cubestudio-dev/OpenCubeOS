<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: crash dumps and the panic path

The panic machinery: exception routing, the panic banner, the
OCDUMP01 core-dump image, the 32-slot crashlog ring, and the
after-panic policy. Implementation:
`ocos/services/crash/src/svc_crash_core.c` (1018 lines) +
`svc_crash_watchdog.c`, the core slots in
`ocos/kernel/arch/x86_64/arch_exceptions.c`, the boot-state relay
in `ocos/kernel/core_boot_state.c` + `include/ocbl_state.h`.
Field usage: [../dev/debug-handbook.md](../dev/debug-handbook.md)
section 3.

## 1. Layering (verbatim from the crash core header)

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

**Trigger**: a ring-0 exception no handler claims, a
`kernel_panic(reason)` call, an `OC_ASSERT(cond)` failure
(`-> kernel_panic("assertion failed: " #cond)`), or a watchdog
policy PANIC. **Impact**: evidence is captured before the machine
stops; the policy decides what happens next.

## 2. Exception routing into the panic path

1. Nested-#PF guard: a #PF while already handling one prints
   `*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***` and
   cli;hlt forever.
2. L1 handler chain (up to `OC_EXC_CHAIN_LEN 4` per vector,
   most-recently-registered first; return 1 = handled/resume).
3. #PF -> `mem_vmm_handle_page_fault` (stack growth within 64 KiB
   below the faulting RSP and above the 8 MiB floor; protection
   violations never remap).
4. Ring-3 origin (`cs & 3 == 3`): SIGSEGV delivery attempt
   (handler + not-in-handler + < 3 deliveries; on-stack restorer
   trampoline), else crashlog record + `*** KILLING user process (pid=N) due to exception V, kernel continues ***` + unified
   reaper - the kernel does NOT panic for user faults. Kills taken
   on IST2 vectors (1/2/3) switch to the dedicated 8 KiB exit
   stack first (BUG-0139) so IST2 is never suspended/resumed.
5. Ring-0 -> panic with stack-overflow attribution: #PF CR2 inside
   a task's guard page -> `"KERNEL STACK OVERFLOW (guard page hit, task=<name>)"`; #DF with rsp at/below the stack bottom or
   implausible garbage (>= 0x40000000000) -> `"KERNEL STACK OVERFLOW (stack exhausted -> #DF, task=<name>)"`; otherwise
   `"ring-0 exception <v> (<name>)"`.

## 3. The register snapshot (verbatim)

```c
typedef struct arch_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} arch_irq_frame_t;
```

22 u64s - matches `OC_DUMP_REGS_U64 22`, so the dumper's register
copy IS this struct. Per-exception reading guide (cr2 for #PF,
error-code bits, rsp-vs-stack-window for #DF):
[../dev/debug-handbook.md](../dev/debug-handbook.md) 3.4.

## 4. The OCDUMP01 image (verbatim header)

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

Image layout in capture order: header, regs (22 u64 or live
snapshot), task block, stack window (up to 4 KiB clipped to
`[stack_base, +size)`), tables (CR3 + PML4 self-scan PTE count).

- **Trigger**: the panic path calls `core_dump_save(reason)`; or
  `crashtest dump` (no panic).
- **Processing**: fills the boot-reserved buffer. `core_dump_alloc()`
  ran once at kmain step 16a with kmalloc - after this point
  kmalloc may fail or the heap may corrupt, but the panic path
  only ever touches the already-reserved buffer. CR3 is
  normalized to the kernel AS before anything else
  (`mem_vmm_switch_as(mem_vmm_kernel_as())`) so
  traceback/dump/heap reads are safe from a user address space.
- **Impact**: persisted to `/var/crash/core.dump` (works on ramfs
  rootfs AND FAT32 mounts); if NO filesystem, the first
  `OC_DUMP_SERIAL_MAX` (2048) bytes are hex-dumped to serial so
  data is not lost. The #DF case skips the dump entirely
  ("core dump: skipped in #DF mode" - stated in KNOWN_ISSUES).

## 5. The crashlog ring (verbatim)

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

32 slots, allocated at boot (`crashlog_boot_alloc()`); every
exception kill and panic source appends; `crashlog_flush()`
appends all entries to `/var/log/crash.log`. Read-back commands:
`crashlog` (each entry
`[i] <src-name> ring=N err=0x.. rip=0x.. rsp=0x.. [cr2=0x..] [@<rtc>s | @tick=N] [task=<name>] [pid=N]`; empty:
`crashlog: no exceptions recorded`) and `crashlog flush`.

## 6. The panic banner format

Serial authoritative, VGA red best effort:

```
*** KERNEL PANIC: <reason|"(no reason)"> [task=<name>] ***
kernel: Open Cube OS <version> (build 0x<hex>)
source: exception <n> (<name>) err=0x<hex>
<task line>
<register dump: 4x4 grid r15..rax + rip/cs/ss/rflags/rsp/int/err [+ cr2] + cr3>
<traceback: rbp chain + stack scan filtered by looks_like_code>
<policy line>
```

Special banners: `[KERNEL STACK OVERFLOW: exhausted, task=<name>]`; `*** NESTED PANIC INSIDE PANIC - HALTING ***`
(the nested-panic guard cli;hlt's); `*** panic handler claimed recovery - resuming ***` / `*** CONTAINED: <what> - isolated, kernel continues ***` (a registered handler claimed recovery -
`crash_contained()` is the non-fatal variant used by the TUI
guard). The #DF banner is deliberately minimal - handled ENTIRELY
at dispatcher depth with one static 192-byte line buffer via
`arch_exc_ser_puts`, one known typo verbatim:
`source: #DF Double Fault err=0x.. rip=0x.. [hhandled at dispatcher depth]`.

## 7. Policy and handlers

- `CRASH_POLICY_HALT 0` (default: cli + safe hlt loop) or
  `CRASH_POLICY_REBOOT 1` (8042 + 0xCF9). `crashtest policy [halt|reboot]` switches live.
- Panic handlers: up to `OC_PANIC_HANDLER_MAX 4`,
  most-recently-registered first; the function is deliberately
  not `noreturn` - returning non-zero claims recovery.
- Core slots are installed at boot by services/crash via
  `arch_set_panic_handlers` (+ `arch_set_crashlog_record`); the
  kernel-side fallback (no crash service yet) prints
  `KERNEL PANIC (no crash handler installed): <reason>` then
  cli;hlt.

## 8. IST discipline and why it matters

IST1 = 8 KiB panic stack (#DF/#MC); IST2 = 8 KiB debug stack
(#DB/NMI/#BP). A fault during a fault handler still reaches the
dumper because #DF runs on IST1. The exception-exit path never
context-switches on IST2 (BUG-0139): kills taken on IST2 vectors
run on the dedicated exit stack first - the scheduler must never
save an rsp inside IST2.

## 9. The boot-state relay (boot failures reach OCIRE)

A hang (not a panic) is caught by a different chain: the
boot-state monitor writes stage/heartbeat/progress to the fixed
low-memory islands (state area 0x98000, reserved area 0x9B000 -
the empirically proven survivors of the QEMU/SeaBIOS reset wipe)
while petting the 6300ESB watchdog. A hang stops the feeding ->
the 6300ESB resets the machine (DRAM kept) -> OCBL reads the
state area, judges (silence >= 8 s without the CLEAN marker; a
manual warm reset measures < ~4 s and is NOT a failure), writes
the BOOT_FAILED record (stall stage, timestamp, measured silence,
ones-complement checksum) -> the next boot auto-boots OCIRE ->
OCIRE reads the record and prints the stall point + repair
advice. A clean `reboot` marks the state area CLEAN first. The
kernel side is **blind-write only** - it never reads the area
back; the 0x98000/0x9B000 pages sit in the supervisor-only half
of every user address space (verified by a CR3 walk at L1_READY).
Structures: [../dev/data-structures.md](../dev/data-structures.md)
section 8. The whole chain is driven live by
`tools/wp07a_state_e2e.py`.

## 10. Testing it

- `crashtest panic|exc|pf|assert|wd|ovf|dump|policy` (diag op 7)
  - the first six REALLY crash the kernel; `dump`/`policy` are
  safe. `exc` = `.byte 0x0F,0x0B` (#UD); `pf` writes
  0x700000000000 (PML4[224], guaranteed not-present).
- `crashlog` + `dumpinfo` prove the round trip after reboot.
- The regression battery checks `crashlog` on every run; the E2E
  `wp11_crash_e2e.py` covers #DE/#UD/#PF self-tests, sigsegv/
  stackovf/badapp kills, crashlog + flush to /var/log, dump +
  dumpinfo.
