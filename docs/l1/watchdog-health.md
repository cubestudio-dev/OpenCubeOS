<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: watchdog and health

## Watchdog - `kernel/core/core_watchdog.h` (18 declarations; numbered 5)

```
watchdog_register(name, timeout_ms, policy)  -> slot
watchdog_start(slot)
watchdog_feed(slot)
watchdog_stop(slot)
watchdog_set_timeout(slot, ms)
```

A registered slot is a *policy* watchdog: the task must feed it;
timeout kills the task (reason logged) while the kernel lives.
Hardware watchdogs (Intel TCO, ACPI WDAT) are layered underneath and
reset the whole machine - the E2E suite proves both with real
resets and second boots.

## Health - `kernel/core/core_health.h` (8 declarations; numbered 5)

```
health_get_cpu(out)
health_get_mem(out)
health_get_tasks(out)
health_get_io(out)
health_export(path)          // valid JSON to /var/log/health.json
```

Thresholds raise alarms into the kernel log (`CPU-LOCKED` on a
pinned core...); `health`, `health watch`, `health export` are the
commands; the E2E asserts the alarm lines and JSON validity.

## klog - `kernel/lib/lib_klog.h` (29 declarations; numbered 6)

```
klog_write(level, fmt, ...)
klog_set_level(lvl)
klog_set_level_enabled(lvl, on)
klog_flush()
klog_rotate()
klog_filter_set(min_level)
```

Levels: DEBUG/INFO/WARN/ERROR/FATAL; rotation
(`kernel.log` -> `kernel.log.1`, threshold 4096) and filtering are
real, E2E-verified behaviours.
