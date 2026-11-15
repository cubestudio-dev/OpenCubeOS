<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the watchdog subsystem

Two watchdog worlds in one subsystem: a **software slot engine**
(8 monitored slots, per-slot policies, 1 s tick) and **hardware
watchdogs** (ACPI WDAT table, Intel TCO on the LPC bridge) - plus
the **6300ESB boot-state relay** that OCBL arms before the kernel
exists. Implementation: `ocos/services/crash/src/svc_crash_watchdog.c`
+ `interface/svc_crash_watchdog.h` (the unified API), the soft
watchdog in `svc_crash_core.c`, the L1 face in
`ocos/services/watchdog/src/svc_watchdog_l1.c` (302 lines), the
boot-state relay in `ocos/kernel/core_boot_state.c`.

## 1. The soft watchdog (crash_wdt)

- `OC_WDT_TIMEOUT_MS 15000` - a **15 s** software watchdog.
- **Trigger**: a periodic core_timer callback checks a heartbeat
  stamp; the shell line editor kicks it on every key.
- **Processing**: if the kernel stops servicing input for 15 s
  *while the dog is armed*, the panic path fires with crash source
  `OC_CRASH_SRC_WDT` (33) - the full WP-11 crash treatment (dump
  + crashlog + policy).
- **Impact**: a wedged console becomes hard evidence instead of a
  silent hang. The soft dog **starts DISARMED** (documented
  decision): long synchronous commands own the shell task and
  cannot kick.
- API: `crash_wdt_init/kick/blackout/armed/ms_since_kick/ force_timeout`.

## 2. The software slot engine (spec 5.2)

Up to 8 monitored slots (`WD_MAX_SLOTS 8`), checked every 1 s
tick while ARMED:

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

Slot 0 = **"system-heartbeat"**: registered by `watchdog_init()`,
wraps the WP-11 shell-heartbeat semantics (default timeout
`OC_WDT_TIMEOUT_MS` 15 s, policy PANIC); `watchdog_feed(0)` is
the L1-visible kick, and the health alarms feed it as an extra
source (an alarmed-but-alive system keeps feeding - see
[health.md](health.md) section 4).

Introspection struct (verbatim):

```c
typedef struct {
    char name[16];
    u32  timeout_ms;
    int  policy;
    int  running;                  /* armed                            */
    u64  last_feed_ms;
    u64  timeouts;                 /* episodes seen so far            */
    int  tid;                      /* creator tid (KILL policy)       */
} watchdog_info_t;
```

Register returns slot id >= 0, -1 table full, -2 bad args
(NULL/empty name, timeout 0, bad policy).

## 3. Hardware watchdogs

Probe order at boot (probed but **NOT armed** - a documented
decision; arming is explicit via `watchdog hw start <sec>`):

1. **ACPI WDAT table** (`driver_acpi_find_table("WDAT")`) - the
   entries' GAS registers drive every action. Under QEMU,
   `tools/make_wdat_table.py` builds a synthetic-but-valid WDAT
   for `-acpitable` injection that drives the real q35 TCO
   registers (the WP-12 E2E gate for WDAT discovery).
2. **Intel TCO** on the LPC bridge (PCI 00:1f.0): PMBASE (config
   0x40) + 0x60 -> TCO_RLD/CNT/TMR. Present on QEMU q35
   (ICH9-LPC); absent on the default `pc` machine (PIIX3) - "the
   probe honestly reports 'none'".

TCO range 2..63 s (`WD_HW_TIMEOUT_MIN/MAX` - the 6-bit TCO timer
field). Soft-hard coordination: the software check runs FIRST on
every tick (WP-11 panic path); the hardware TCO stays the
last-resort hard reset - **"the hardware timeout is set >= 2x the
software one whenever the framework arms both"** (the dual-arm
rule: hardware >= 2 x software, so the software panic path always
gets its chance to produce a dump before the silicon resets).

Hardware API: `watchdog_hw_present/name/start(seconds)/feed/stop/ set_timeout/timeout/base/armed/gcs_noreboot` (the GCS bit5
live readback is the "no-reboot" status).

## 4. The 6300ESB boot-state relay

The third watchdog, owned by the boot-state monitor rather than
the watchdog service: OCBL arms the **Intel 6300ESB** watchdog
(the same PCI device OCIRE's supervisor loop uses) before
handing off to the kernel; `core_boot_state.c` takes it over via
raw PCI/IO cycles (config 0x60 latches on 16-bit writes, the lock
register 0x68 only on 8-bit writes, RELOAD needs the 0x80/0x86
unlock dance; probe = PCI 8086:25AB, BAR0 resolved I/O or MMIO).
Phase budgets while it is armed: 30 s boot start -> stage 1,
10 s per middle phase, 30 s L1_LAUNCHED -> L1_READY, **15 s
runtime heartbeat** petted from the 1 s tick. A hang stops the
feeding -> the 6300ESB resets the machine (warm reset, DRAM kept)
-> OCBL judges -> the record routes to OCIRE. `/HANGAT`
(`oc.hang=<stage>`) exercises the real relay chain deliberately;
`oc.stmon=off` disables the whole monitor. On OCIRE-supervised
(kexec) boots, the embed service disarms the i6300esb inside the
supervisor's 60 s window (see
[ocembed-guard.md](ocembed-guard.md)).

## 5. The L1 face (1 op carries the whole grammar)

```c
#define L1SVC_WDT_CMD 1     /* watchdog [subcommand ...]     */
```

Subcommands: `status` (default; hw name/armed/timeout/base + slot
table), `slots`, `hw status|start [sec=30]|stop|feed|timeout <2..63>`, `start|stop|feed <id>`, `timeout <id> <ms 100..3600000>`, `register <name> <ms> <0=panic 1=reboot 2=kill 3=log>`, `test kill|panic|log` (spawns the `wd-starve` kthread
via `core_kthread_create`, priority 15, registers itself
monitored 3 s and never feeds - "expect the policy in 3 s").

Notable strings: `watchdog hw: ARMED - machine RESETS if nobody feeds`, `watchdog hw: none (no WDAT table, no Intel TCO LPC)`,
`watchdog hw: no hardware watchdog present`,
`watchdog: register FAILED (table full?)`, `watchdog: bad slot id`.

## 6. Using a slot from kernel code

```
slot = watchdog_register("mydaemon", 5000, WD_POLICY_KILL);
watchdog_start(slot);
... watchdog_feed(slot);   /* at least every 5 s */
```

Timeout fires -> the POLICY runs: the task is killed and the
reason lands in the kernel log (KILL), the kernel panics (PANIC),
the machine reboots (REBOOT), or one ERROR per episode (LOG -
re-arms on the next feed). This is the right tool for "my
background worker may hang"; the kernel lives on under KILL/LOG.

## 7. The E2E scenarios that gate this

S7 (status/slots), S8 (policy kill + log), **S12: Intel TCO real
reset + second boot** (q35, `-no-reboot`: discover, arm, starve,
assert the second boot), **S13: ACPI WDAT real reset + second
boot**. Fourteen total in `tools/wp12_watchdog_logging_e2e.py` -
all fourteen must PASS at release.
