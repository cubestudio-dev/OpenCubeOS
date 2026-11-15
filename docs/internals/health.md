<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the health monitor

The 1-second sampler over five metric dimensions, three deduplicated
alarm bits, and the alarm-to-watchdog feed linkage.
Implementation: `ocos/services/health/interface/svc_health_core.h`
(101 lines), `src/svc_health_core.c` (~313), `src/svc_health_l1.c`
(181). Usage: [../dev/debug-handbook.md](../dev/debug-handbook.md)
4.1.

## 1. One sample (verbatim)

```c
typedef struct {
    u64  ts_ms;             /* core_timer_now_ms() at sample time   */
    u32  cpu_busy_pct;      /* 0..100 over the last window          */
    u64  mem_total_kb;      /* physical RAM in KiB                  */
    u64  mem_used_kb;
    u64  mem_free_kb;
    u64  heap_used_bytes;   /* kmalloc arena                        */
    u64  heap_free_bytes;
    u32  tasks_total;       /* scheduler slots in use               */
    u32  tasks_running;
    u32  tasks_ready;
    u32  tasks_blocked;
    u64  disk_reads;        /* cumulative since boot                */
    u64  disk_writes;
    u64  disk_read_bytes;
    u64  disk_write_bytes;
    u64  net_tx_packets;
    u64  net_rx_packets;
    u64  net_tx_bytes;
    u64  net_rx_bytes;
    u32  alarms;            /* bitmask of live alarms (see below)   */
} health_sample_t;

#define HEALTH_ALARM_MEM    (1u << 0)   /* memory > 90% used          */
#define HEALTH_ALARM_TASKS  (1u << 1)   /* active tasks > 64           */
#define HEALTH_ALARM_CPU    (1u << 2)   /* 100% busy for 10 samples    */
```

## 2. The five metric dimensions and their sources

Sampled every second (`health_init()` at kmain step 13; boot log
line: `monitor started (1 s samples, alarms: mem>90%% tasks>64 cpu>=90%%x10s)`):

| Dimension | Source | Notes |
|---|---|---|
| CPU | `core_sched_idle_counters()` deltas | busy% = 100 - idle% over the window |
| Memory | `mem_pmm_get_stats()` + `mem_heap_get_stats()` | physical + kmalloc arena |
| Tasks | `core_sched_get_stats()` + task-state walk | total/running/ready/blocked |
| Disk | `driver_block_get_stats()` summed | sectors -> bytes at 512 |
| Network | `net_get_stats()` | cumulative tx/rx packets + bytes |

## 3. Judgment logic (the three alarm bits)

Alarms are evaluated once per sample and **deduplicated** - one
ERROR klog per threshold crossing, re-armed when the metric
returns under the threshold (module "health"):

- `mem_used*100/mem_total > 90` -> `HEALTH_ALARM_MEM`;
  klog: `alarm: memory >90%% used (%u/%u KiB)`
- `tasks_total > 64` -> `HEALTH_ALARM_TASKS`;
  klog: `alarm: %u active tasks (>64)`
- `cpu_busy_pct >= 90` bumps `g_cpu_100_run`; >= 10 consecutive
  samples -> `HEALTH_ALARM_CPU`;
  klog: `alarm: CPU >=90%% busy for 10 consecutive seconds`
  (the header comment says "100%" but the code gates at >= 90 for
  10 s - the implementation note is honest about the difference)

**Trigger** for the whole mechanism: the 1 s tick.
**Processing**: counters are read, deltas computed, alarms
evaluated with the dedup state machine.
**Impact**: the `alarms:` line in every snapshot
(`MEMORY>90% TASKS>64 CPU-LOCKED` or `none`) and the klog ERROR
lines - the first objective evidence in any silent-failure
investigation.

## 4. The alarm-to-watchdog feed linkage

The alarms feed the soft-slot table's system-heartbeat slot
(slot 0, registered by `watchdog_init()`, the WP-11 shell-heartbeat
semantics, 15 s default, policy PANIC) as an **extra feed source**:

- an **alarmed-but-alive** system keeps feeding the dog (the
  sampler proves the kernel is scheduling);
- a **dead** one stops feeding, and the WP-11 panic path takes
  over: full crash path (dump + crashlog + policy) with crash
  source `OC_CRASH_SRC_WDT` (33).

This is the deliberate split: alarms are *observations* (log +
display), the watchdog slot is the *enforcement* (a genuinely dead
kernel gets the panic treatment, an overloaded one does not get
reset just for being busy). See [watchdog.md](watchdog.md).

## 5. The ring-3 face (3 ops)

| Op | Command | Behavior |
|---|---|---|
| `L1SVC_HEALTH_SNAPSHOT` (1) | `health` | v2.2.0 report text - sections cpu/memory/tasks/disk/network with the `hc_line()` style (label space-padded to column 22, then value + unit), plus the `alarms:` line |
| `L1SVC_HEALTH_EXPORT` (2) | `health export [path]` | `health_export()` JSON (`{"ts_ms":1234,"cpu_busy_pct":7,...}`) written via fs_vfs to arg (default `/var/log/health.json`), then printed |
| `L1SVC_HEALTH_WATCH` (3) | `health watch` | interactive 1 s refresh loop with direct console output, ENTER exits (`health watch: refreshing every second (press ENTER to exit)` / `-- watch stopped --`) |

Errors: `health: export FAILED`, `health: export FAILED (no filesystem mounted?)`.

## 6. Reading a snapshot in the field

The report sections in order (cpu / memory / tasks / disk /
network), each line `hc_line()`-shaped - label space-padded to
column 22, then the value + unit - and a final `alarms:` line
(`MEMORY>90% TASKS>64 CPU-LOCKED` or `none`):

```
cpu:
  <label>                <value> %
memory:
  <label>                <value> KiB
tasks: / disk: / network:
  ...
alarms: none
```

Interpretation guide for the silent-failure ladder
([../dev/debug-handbook.md](../dev/debug-handbook.md) 4.1):

| Symptom | Look at | Next step |
|---|---|---|
| Machine "slow" | cpu busy + `CPU-LOCKED` | `sched`, `ps` - find the spinner; `nice <tid> <prio>` it down |
| Alloc failures, mount failures | memory + `MEMORY>90%` | `mem`, `frag` (fragmentation vs exhaustion), `heap` |
| New processes refuse to start | tasks + `TASKS>64` | `ps` for unreaped/zombie-shaped rows |
| Disk feels wedged | disk deltas between two snapshots | `dskstat`, `sync`, the block-cache stats in `lsblk` |
| Network silent | net tx/rx deltas | `netstat`, `tcpstats`, the driver status ops |

Two snapshots a few seconds apart turn the cumulative counters
into rates - that is exactly what `health watch` shows live
(1 s refresh, ENTER exits).

## 7. Verification

The E2E (`tools/wp12_watchdog_logging_e2e.py`) exercises the
health groups: S11 runs 3 spinner tasks and asserts the CPU alarm
while the kernel keeps responding; the JSON export scenario
asserts a parseable `/var/log/health.json` (the standard metric
set, ~386-389 bytes) - including from the /etc volume after a
reboot, parseable by any monitoring tool. The alarm dedup is
asserted too: the ERROR klog line appears once per crossing, not
once per second, and re-arms when the metric recovers.
