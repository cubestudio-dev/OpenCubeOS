# Open Cube OS - WP-12-watchdog_logging Extension Interfaces

SPDX-License-Identifier: Apache-2.0
Copyright 2026 cubestudio-dev <cubestudio@qq.com>

WP-12 adds the watchdog + logging + health layer: a unified five-level
kernel log with per-level switches, four output channels (serial COM1,
VGA console, a 1024-entry ring, `/var/log/kernel.log` on disk) with
rotation and filters; a software watchdog framework with task-level
monitored slots and four timeout policies; a hardware watchdog driver
(Intel TCO on the LPC bridge, discovered via PMBASE, plus a full ACPI
WDAT table parser) whose expiry hard-resets the machine; and a 1 s
system health monitor (CPU / memory / tasks / disk I/O / network I/O)
with deduplicated alarms and a JSON export.  Every interface below has
a header declaration with comments, a working implementation, a
documented error contract, and a QEMU-verified test (the
`tools/wp12_watchdog_logging_e2e.py` gate runs all 14 scenarios end to
end across the pc / q35 / q35+WDAT machine shapes).

## Files

| file                              | purpose                                        |
|-----------------------------------|------------------------------------------------|
| kernel/lib/lib_klog.h/.c          | unified kernel log (5 levels, 4 channels)     |
| kernel/core/core_watchdog.h/.c    | soft slots + TCO/WDAT hardware watchdog       |
| kernel/core/core_health.h/.c      | 1 s sampler + alarms + JSON export            |
| kernel/core/core_sched.c/.h       | idle/busy tick counters + task-state walk     |
| kernel/lib/lib_log.c/.h           | boot log mirrored into the klog ring          |
| kernel/main.c                     | init order + boot banner count (233)          |
| shell/shell_cmds_log.c/.h         | dmesg / logcat / logctl commands              |
| shell/shell_cmds_wd.c/.h          | watchdog command                              |
| shell/shell_cmds_health.c/.h      | health / health watch / health export         |
| shell/shell.h                     | SHELL_MAX_COMMANDS 232 -> 256 (fix5)          |
| tools/wp12_watchdog_logging_e2e.py| 14-scenario acceptance gate                   |
| tools/make_wdat_table.py          | synthetic WDAT for -acpitable injection       |

## Unified kernel log (klog)

```c
#define KLOG_DEBUG 0
#define KLOG_INFO  1
#define KLOG_WARN  2
#define KLOG_ERROR 3
#define KLOG_FATAL 4

int  klog_write(int level, const char *module, const char *fmt, ...);
int  klog_set_level(int level);        /* live gate: -1 mute .. 4 FATAL */
int  klog_flush(void);                 /* append ring -> /var/log/kernel.log */
int  klog_rotate(void);                /* kernel.log -> .1 -> .2 -> .3   */
void klog_filter_set(int level, const char *module, const char *keyword);
void klog_filter_clear(void);
```

`klog_write` formats into the 1024-entry ring (64-entry static
bootstrap ring before `klog_grow()`; the identity-window ASSERT is
kept by growing on the heap after `mem_heap_init`), mirrors to the
serial + VGA live channels when `level >= klog_get_level()`, appends
to `/var/log/kernel.log` when the file channel is open, and rotates
(flush-first, `kernel.log -> kernel.log.1`, keep 3) when the file
crosses `klog_set_rotate_bytes()` (default 1 MiB; `logctl threshold`
sets 512 B .. 16 MiB).  IRQ-context writers only set the dirty flag;
the file append happens in task context (and `klog_flush_panic()`
forces it on the panic path, for non-#DF sources - the 15.13 boundary
is unchanged).  Every level has an independent on/off switch
(`klog_set_level_enabled`): a disabled level is neither printed nor
recorded.  The filter (level >=, module, keyword substring) applies to
the live channels AND to `dmesg`/`logcat` replay.  `lib_log` mirrors
every boot stage line into the ring, so a fresh boot's `dmesg` shows
the full `[0.000] INFO  boot: ...` sequence.

Replay format (one line per entry):
`[<sec>.<ms>] LEVEL  <module>: <message>` - level names are stored
unpadded and padded to a 6-column field at print time (so filters
match `INFO` exactly, not `INFO ` ).

Error contract: `klog_write` returns -1 (bad level/NULL), otherwise
the formatted length.  `klog_flush`/`klog_rotate` return -1 when no
filesystem is mounted.  `klog_set_level` returns -1 for level < -1 or
> 4.

## Software watchdog

```c
#define WD_POLICY_PANIC  0   /* kernel_panic("watchdog timeout: ...")  */
#define WD_POLICY_REBOOT 1   /* klog ERROR + core_power_reboot()       */
#define WD_POLICY_KILL   2   /* klog ERROR + core_sched_task_exited()  */
#define WD_POLICY_LOG    3   /* klog ERROR once per episode            */

int watchdog_register(const char *name, u32 timeout_ms, int policy);
int watchdog_start(int id);
int watchdog_feed(int id);
int watchdog_stop(int id);
int watchdog_set_timeout(int id, u32 timeout_ms);
```

A 1 s kernel tick scans the slot table.  A slot that started and has
not been fed for its timeout fires its policy exactly once per
episode: PANIC takes the full WP-11 panic path (banner + registers +
traceback + dump + crashlog + policy), REBOOT resets through the 8042,
KILL reaps only the monitored task (the kernel keeps running), LOG
records one ERROR and disarms until the next feed.  Slot 0 wraps the
WP-11 system heartbeat (`system-heartbea`, 15 s, PANIC): the health
sampler feeds it every second, so a live machine stays silent and a
locked scheduler stops feeding -> panic.  Boot default stays
DISARMED (the WP-11 decision, documented in EXTENSIONS_WP11.md: long
synchronous shell commands own the shell task and cannot feed).

Error contract: `watchdog_register` returns -1 (NULL name / bad policy
/ table full, 16 slots); `watchdog_start/feed/stop/set_timeout` return
-1 (bad id), feed on a stopped slot returns 0 and resets the clock.

## Hardware watchdog (Intel TCO + ACPI WDAT)

```c
int  watchdog_hw_present(void);        /* 1 = TCO or WDAT found       */
const char *watchdog_hw_name(void);   /* "Intel TCO"/"ACPI WDAT"/""  */
int  watchdog_hw_start(u32 seconds);   /* arm + set the countdown     */
int  watchdog_hw_feed(void);          /* kick (TCO_RLD / WDAT ping)  */
int  watchdog_hw_stop(void);
int  watchdog_hw_timeout(void);        /* current countdown, seconds  */
```

Discovery: the LPC bridge (00:1f.0) PMBASE (offset +0x60) locates
TCOBASE (+0x60); the RCBA is enabled through PCI config 0xF0 and GCS
NO_REBOOT (bit 5 at RCBA+0x3410 - the real offset, not the 0x3418
folklore) is cleared on arm, the `iTCO_wdt update_no_reboot_bit` step
without which QEMU never resets (found live via
`-trace tco_timer_expired`).  When an ACPI WDAT table is present it
takes precedence: the parser honours the table's own Header Length,
reads the 24-byte entries (action, instruction, GAS, value@+16,
mask@+20) and drives SET_COUNTDOWN / RUNNING / STOP / SET_REBOOT
through SystemIO and SystemMemory GAS entries with the table's own
value/mask.  WDAT expiry hard-resets the machine - on real hardware
the reset is the point; in QEMU the second boot reaching the shell is
the proof.  A plain pc machine reports `none` honestly (no WDAT, no
Intel TCO LPC).

Error contract: `watchdog_hw_start` returns -1 (no hardware / bad
timeout, 2..63 s), `watchdog_hw_feed/stop` return -1 when not armed.

## System health monitor

```c
int health_get_cpu(u32 *busy_pct);     /* 1 s window, idle/total ticks */
int health_get_mem(u64 *total_kb, u64 *used_kb, u64 *free_kb);
int health_get_tasks(u32 *total, u32 *running, u32 *ready, u32 *blocked);
int health_get_io(u64 *disk_read_bytes, u64 *disk_write_bytes,
                  u64 *net_tx_bytes, u64 *net_rx_bytes);
int health_export(char *out, int cap); /* single JSON object          */
```

A 1 s periodic sampler (IRQ-safe counter reads + a static publish)
computes CPU busy% over the window from the scheduler's new
idle/total tick counters (measured 67% with one spinning task, 96%
with three, 100% with three plus the sampler's own budget), memory
from PMM + heap stats, the task state split from a scheduler walk,
disk bytes summed over block devices, and network counters.  Alarms
are deduplicated on the rising edge: `mem > 90%`, `tasks > 64`,
`cpu >= 90%` for 10 consecutive samples (the empirical ceiling on the
preempted single core - three spinners measure 96%, so a ==100%
threshold would never fire).  The sampler feeds the system heartbeat
slot every second.

`health_export` renders one JSON object:
`{"ts_ms":...,"cpu_busy_pct":...,"mem_total_kb":...,"mem_used_kb":...,
"mem_free_kb":...,"heap_used_bytes":...,"heap_free_bytes":...,
"tasks_total":...,"tasks_running":...,"tasks_ready":...,
"tasks_blocked":...,"disk_reads":...,"disk_writes":...,
"disk_read_bytes":...,"disk_write_bytes":...,"net_tx_packets":...,
"net_rx_packets":...,"net_tx_bytes":...,"net_rx_bytes":...,
"alarms":...,"alarms_text":"none|MEMORY>90% TASKS>64 CPU-LOCKED"}`.

Error contract: the getters return 0 and always write their outputs
(before init they read as zero); `health_export` returns the rendered
length or -1 when the buffer cannot hold the object.

## Shell commands

| command         | purpose                                              |
|-----------------|------------------------------------------------------|
| `dmesg`         | ring replay, `dmesg [N]` caps at N lines (default 30)|
| `logcat`        | `-l level -m module -k keyword -t seconds -n count`, `-f` follow |
| `logctl`        | status / level / enable / disable / clear / flush / rotate / threshold / export / filter / test |
| `watchdog`      | status / slots / hw status|start|stop|feed|timeout / start|stop|feed|timeout / register / test kill|panic|log |
| `health`        | full live snapshot (all metric groups + alarms)      |
| `health watch`  | 1 s refresh loop until ENTER                         |
| `health export` | write the JSON to /var/log/health.json [path] + print|

Command count: 226 -> 233.  `help` three-mode output unchanged for
every pre-existing command.  `health watch` / `health export` are
registered verbatim as their own table entries (the spec's 7-command
list): the unquoted `health watch` keeps the health-subcommand path,
a quoted `"health watch"` dispatches the dedicated handler - both run
the identical watch/export core.

## Error contract quick reference

| call                    | success          | failure                          |
|-------------------------|------------------|----------------------------------|
| klog_write              | formatted length | -1 (bad level / NULL)            |
| klog_flush / klog_rotate| 0 / generations  | -1 (no filesystem)               |
| klog_set_level          | 0                | -1 (level < -1 or > 4)           |
| watchdog_register       | slot id          | -1 (NULL/bad policy/table full)  |
| watchdog_start/feed/... | 0                | -1 (bad id / not armed)          |
| watchdog_hw_start       | 0                | -1 (no hw / timeout 2..63 s)     |
| health_get_*            | 0                | - (always writes, zeros pre-init)|
| health_export           | rendered length  | -1 (buffer too small)            |

## Bring-up findings (all fixed in-tree, comments at the sites)

- Live klog lines printed twice on serial (the console hook already
  mirrors; the direct serial write was removed).
- INFO/WARN filter tags were padded to 5 chars and unmatchable; level
  names are now stored unpadded and the format pads to 6 columns.
- WDAT `RUNNING` action passed the literal 1 (halted the dog instead
  of running it); entry stride corrected 28 -> 24 bytes.
- The GAS space filter dropped the SystemMemory GCS entry.
- Probe order swapped so the RCBA enable side effect precedes the
  WDAT-mode selection.
- health heap field names (allocated/free) and the KiB conversion
  divided by 4 instead of multiplying (524160 KiB read as 32760).
- CPU alarm threshold ==100% unreachable on the preempted single core
  (three spinners measure 96%) -> >=90% for 10 consecutive samples.
- `SHELL_MAX_COMMANDS` 232 filled exactly at WP-12: 226 (WP-11) +
  dmesg/logcat/logctl/watchdog/health/health watch = 232, and the
  `health export` registration was silently refused - boot showed
  232, not 233 (the same undercount class as fix4).  Raised to 256;
  the table is heap-allocated since group B, so no BSS cost.

## Acceptance

`tools/wp12_watchdog_logging_e2e.py <iso>` - 14/14 scenarios:
S1-S11 on the pc machine (levels, switches, filters, replay caps,
control plane, persistence + REAL rotation chain, global filter,
slots, kill/log policies through the REAL 3 s timeout, health
snapshot, JSON export, 3-spinner CPU alarm with the kernel alive),
S12 on q35 (TCO discovery + `hw start 5` -> REAL hardware reset ->
second boot), S13 on q35 + the synthetic WDAT (`ACPI WDAT found: 4
entries` -> REAL reset -> second boot).  Full gate: WP-12 E2E 14/14,
regression 28/28, dhtest 5/5, SSH E2E 5/5, HTTPS E2E 7/7, BIOS + UEFI
boots, 0 errors / 0 warnings.
