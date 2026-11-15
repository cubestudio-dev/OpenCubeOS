<!-- SPDX-License-Identifier: Apache-2.0 -->
# Watchdog, health and klog from apps

## Watchdog (service `watchdog`, L1SVC cat 11)

A registered slot is a *policy* watchdog: the owning task must
feed it; a timeout kills the task (reason logged) while the kernel
lives. Hardware watchdogs (Intel TCO, ACPI WDAT) are layered
underneath and reset the whole machine - the E2E suite proves
both with real resets and second boots.

```
watchdog_register(name, timeout_ms, policy)  -> slot
watchdog_start / feed / stop / set_timeout
```

`ush> watchdog` (or the sysdiag classic table) reports slots and
policies; the starve test (`wd-starve`, 3000 ms) is how the E2E
proves the kill path.

## Health (service `health`, L1SVC cat 9)

```
health_get_cpu / mem / tasks / io      (the snapshot sources)
health_export(path)                    (valid JSON)
```

Thresholds raise alarms into the kernel log (`CPU-LOCKED` on a
pinned core, ...). The app surface: `health` (snapshot - the
sysdiag wizard's first menu item), `health watch` (live monitor,
ENTER exits), `health export [path]`.

## klog (service `klog`, L1SVC cat 10)

`dmesg [N]` (the ring, newest last), `logcat` (structured: -l
level -m module -k key -t seconds -n N -f follow), `logctl
status|level|...` (runtime control). The sysdiag log-tools submenu
fronts all three.

## The mega-app view

`sysdiag` groups the whole observability story: health snapshot,
kernel log, memory picture, scheduler stats, crash records, log
tools, selftest lab. Watchdog + health + klog stay individually
reachable (classic/direct) for scripts and the E2E suites.
