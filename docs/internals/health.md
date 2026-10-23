<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the health monitor

`kernel/core/core_health.{c,h}` - 8 declarations; the numbered 5:

```
health_get_cpu(out)
health_get_mem(out)
health_get_tasks(out)
health_get_io(out)
health_export(path)
```

## What the metrics see

- **CPU**: per-core busy accounting over a 1 s window - three
  spinner tasks measurably pin the core (100% observed live; >= 90%
  sustained across 10 consecutive samples is the alarm threshold).
- **Memory**: PMM/heap state.
- **Tasks**: process table census.
- **IO**: block/net counters.

## Alarms

Threshold crossings write to the kernel log: `CPU-LOCKED` when a
core stays pinned (the E2E S11 runs 3 spinners and asserts the alarm
while the kernel responds), memory pressure, IO stall.

## Export

`health export` writes valid JSON to `/var/log/health.json` (386-389
bytes of the standard metric set in the E2E) - parseable by any
monitoring tool, including from the /etc volume after a reboot.

## Commands

`health` (summary), `health watch` (1 s live), `health export`.
