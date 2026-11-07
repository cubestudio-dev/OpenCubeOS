<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the kernel log (klog)

`kernel/lib/lib_klog.{c,h}` - 29 declarations; the numbered 6:

```
klog_write(level, fmt, ...)      /* the entry point */
klog_set_level(lvl)
klog_set_level_enabled(lvl, on)
klog_flush()
klog_rotate()
klog_filter_set(min_level)
```

## Behaviour (E2E-verified, not aspirational)

- Levels DEBUG/INFO/WARN/ERROR/FATAL; per-level enable + a global
  minimum filter.
- The ring buffer mirrors to serial; the persistent file
  (`kernel.log` on /etc) flushes on demand.
- Rotation at the 4096-byte threshold: `kernel.log` becomes
  `kernel.log.1` (a real generational pair - S5 asserts both sizes).
- Filter proof: level >= 3 admits ERR/FATAL only while dbg/inf/wrn
  counts go to zero (S6's exact assertion).

## Commands

`dmesg` (ring buffer), `logcat` (live follow), `logctl`
(levels/filters), `crashlog` (crash records).

## Who writes

Every subsystem's error paths + the health alarms + the watchdog
policy kills - the log IS the system narrative; `logctl` is how you
turn the noise down without losing the errors.
