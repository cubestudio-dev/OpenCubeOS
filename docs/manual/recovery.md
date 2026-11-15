<!-- SPDX-License-Identifier: Apache-2.0 -->
# Recovery

## Crash artifacts

When the kernel catches a fault it writes a crash record before the
report path:

```
ush> sysdiag               -> crash records (crashlog + dumpinfo)
ush> crashlog              (the recorded exceptions of this boot)
ush> dumpinfo              (core-dump status)
ush> dmesg                 (the kernel log around the event)
```

`crashtest panic|exc|...` (classic/direct only - deliberately not a
wizard button) injects controlled faults to exercise the machinery.

## The A/B escape hatch

A bad slot never bricks the machine:

1. Reboot; the GRUB menu offers force-slot-A / force-slot-B.
2. Boot the other slot, then from inside it:

```
ush> update rollback
```

## The WinRE-style recovery flow

1. Boot any Open Cube OS ISO (the rescue kit is the installer).
2. Attach the broken disk (`-drive if=ide,file=broken.img`).
3. Survey without writing:

```
ush> disk-tool               lsblk / parted / fsck - all read-only here
ush> files                   browse the damaged volume once mounted
```

4. Recover data with `files` (cp to a mounted FAT32 volume).
5. Reinstall if needed: `install <dev>` (single-system) or the
   `disk-tool` A/B manager (update-disk shape).

## The crash dump subsystem

Kernel core dumps land under the crash service's artifacts area
with register + stack state; `dumpinfo` reports where. The
[../internals/crash-dump.md](../internals/crash-dump.md) page has
the format and the retention policy.

## Watchdog

The watchdog service guards liveness; a hung kernel trips a reset
and the next boot's crashlog shows the watchdog event with the
stall context. `watchdog` (classic / `sysdiag` table) reports
status; the internals are in
[../internals/watchdog.md](../internals/watchdog.md).
