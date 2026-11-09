<!-- SPDX-License-Identifier: Apache-2.0 -->
# The recovery workflow (WinRE-style)

Open Cube OS *is* the recovery environment. Where Windows hides WinRE
behind a boot failure, here the `oc>` shell is the primary interface -
and it doubles as the rescue kit for the machine it runs on.

## Posture

- The system boots to a shell even with **no disk at all** (the ISO
  is self-contained).
- Every diagnostic is a shell command away - no separate rescue
  medium to build, no special mode to enter.
- Crash artifacts persist to the /etc volume, so a machine that
  reboots after a panic still carries its own evidence.

## Crash artifacts

When the kernel panics (unhandled exception outside the guarded
self-test):

1. The exception frame (registers, fault address) dumps to serial -
   you see it with `-serial file:log.txt`.
2. A crashlog entry is written to the /etc volume.
3. The machine halts (or reboots, per the watchdog policy).

After rebooting:

```
oc> dumpinfo            (inspect the saved crash dump)
oc> dmesg               (kernel ring buffer from the current boot)
oc> logcat              (follow the persistent kernel log live)
oc> ls /etc             (crashlog files live here)
```

## Forcing a crash (to test the path)

```
oc> crashtest           (deliberate kernel panic - safe on a scratch
                          system, everything not on /etc is lost)
```

`crashtest` proves the dump path end-to-end: panic -> serial dump ->
/ etc crashlog -> `dumpinfo` reads it back.

## The rescue kit

Typical rescue scenarios and their commands:

| Scenario | Do this |
|----------|---------|
| System boots, then misbehaves | `dmesg`, `logcat`, `health`, `health export` for a JSON snapshot |
| A user program hangs | task list + kill through the process commands; `watchdog` policy kills starvers |
| Suspicious disk | `dskstat`, `fsck <dev>`, mount read-only to inspect |
| Boot flag points at a bad slot | GRUB menu -> force the other slot; then `update rollback` |
| Lost network config | `config restore` (defaults) then re-run `dhcp` |
| Log overflow | `logctl` (level + filters), rotation is automatic (`kernel.log` -> `kernel.log.1`) |

## Watchdogs (the hardware side)

`kernel/core/core_watchdog.c` arms real hardware watchdogs where the
platform has them:

- **Intel TCO** (LPC bus): discovered live on QEMU q35 - the E2E
  suite shows `Intel TCO found: PMBASE=0x600 TCOBASE=0x660`, arms it,
  starves it, and the machine genuinely hard-resets and boots again.
- **ACPI WDAT**: the table's entries are programmed; arming and the
  reset are verified the same way.

```
oc> watchdog            (status: hardware support + monitored slots)
oc> watchdog feed       (feed the active watchdog)
oc> watchdog start/stop/timeout ...   (see the command page)
```

Monitored slots additionally implement a *policy* watchdog: tasks
that must periodically feed their slot get killed (with the reason
in the log) without taking the kernel down - the kernel survives,
the offender does not.

## Health monitoring

```
oc> health              (CPU / memory / tasks / IO at a glance)
oc> health watch        (live monitor, 1 s refresh)
oc> health export       (JSON snapshot to /var/log/health.json)
```

Thresholds raise alarms into the kernel log (`CPU-LOCKED` when a
spinner pins a core, memory pressure, IO stall) - the alarm lines are
what the E2E suite asserts on.
