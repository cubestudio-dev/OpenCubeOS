<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCBL: dual-firmware boot and A/B slot decisions

How OCBL picks what to boot, how the A/B slots on the recovery disk
are judged, and what the two low-memory state areas mean.  The
contract is shared byte-for-byte by three independent builds - OCBL
(`ocbl/`), the OCOS kernel (`src/L0/`) and the OCIRE recovery kernel
(`ocire/`) - in `include/ocbl_state.h`.

## The A/B recovery disk: two independent physical partitions

The WP-07 recovery disk carries OCIRE in **two independent physical
partitions**:

| Slot | Partition | Start LBA | Size | Filesystem |
|---|---|---|---|---|
| OCIRE-A | p1 | 2048 | 64 MiB | real FAT32, LFN read support |
| OCIRE-B | p2 | 133120 | 64 MiB | real FAT32, LFN read support |

This is a genuine two-partition layout - not one partition holding
two image files.  The older FAT12 0xEE shadow-volume media still
mounts (compatibility); on the current media the shadow is retired.

The slot selection block is the **A/B flags sector at LBA 1024**,
between the OCBL image and the first partition - a 24-byte
`OCIREAB1` structure whose layout OCBL and OCIRE share
byte-for-byte:

| Field | Meaning |
|---|---|
| `magic[8]` | `"OCIREAB1"` |
| `active_slot` | 1 = A, 2 = B (the last confirmed-good slot) |
| `try_slot` | 0 = none; otherwise the candidate being tried |
| `attempts` | boot attempts made on `try_slot` |
| `flags` | bit 0 = dirty |
| `boot_count` | successful OCIRE boots |

## Slot decision flow

1. OCBL reads the flags sector and picks the slot: `try_slot` if one
   is being tried, otherwise `active_slot`.
2. It bumps `attempts` for the candidate **before** loading it.
3. If the candidate will not load, OCBL falls back to the other slot.
4. A slot that runs is **confirmed by OCIRE** (promotion): the
   recovery kernel updates the flags sector on a good boot, which is
   what makes `active_slot` honest.

The OCOS A/B rollback uses the same protocol shape as the GRUB
config it replaced (`next_B` / `ok_B` / `bootfail_B` on the first
system volume of a disk) - the takeover menu's Rollback key flips
exactly those flags.

## The two state areas

Two fixed low-memory blocks implement the boot-state monitor.  Both
were live-measured to survive warm resets on the reference platform
(the reset wipe stops at the EBDA, 0x9FC00); a **cold boot or power
loss clears them**, so every consumer validates the magic and the
ones-complement checksum before trusting a record - no record simply
means a normal boot, never a false failure.

| Area | Address | Size | Written by | Read by | Meaning |
|---|---|---|---|---|---|
| STATE | `0x98000` | 4 KiB | the kernel, **blind** (it never reads it back); OCBL re-inits at handoff | OCBL, OCIRE | what the kernel is doing right now: stage, heartbeat, progress clock |
| RESERVED | `0x9B000` | 4 KiB | OCBL (the judgment or the user request) | OCIRE, after the reboot | the post-mortem boot record |

Both pages are supervisor-only in every user address space - L1
applications cannot reach them - and both are carved out of the
multiboot2 memory map as E820 reserved so no payload ever allocates
them.

### The boot stages (the STATE area's `stage` field)

Numeric order is boot order; the kernel only ever moves forward:

| # | Stage | Meaning |
|---|---|---|
| 0 | BOOT | pre-kmain handoff |
| 1 | KMAIN_ENTERED | kmain() entered |
| 2 | SCHED_READY | scheduler + core mechanisms |
| 3 | DRIVERS_READY | driver init complete |
| 4 | SERVICES_READY | root services ready |
| 5 | L1_LAUNCHED | default L1 app started |
| 6 | L1_READY | ush served (first syscall) |
| 255 | RUNTIME | marker: hung while running |

## Failure counting and the clear condition

- A **single** judged boot failure bumps the record's `boot_fails`
  counter and **retries OCOS**.
- When the counter reaches **2** (`OCST_BOOT_FAILS_ROUTE`), OCBL
  auto-routes the machine into OCIRE - the takeover.
- The kernel **clears the counter after a confirmed good boot**
  (L1_READY reached), so "consecutive" is honest: one bad boot
  followed by a good boot leaves the counter at zero.
- The counter rides the record's former reserved byte - the record
  layout is unchanged, older readers keep working.

## The watchdog relay (who times whom)

After the multiboot2 handoff OCBL is no longer executing, so the
per-phase timeouts are enforced by the **i6300ESB hardware
watchdog**: OCBL arms the boot budget before the handoff, the kernel
re-arms the phase budget at every stage advance and pets the timer
from its 1-second tick at runtime.

| Phase | Budget |
|---|---|
| boot start -> KMAIN_ENTERED | 30 s (armed by OCBL pre-handoff) |
| each intermediate phase | 10 s |
| L1_LAUNCHED -> L1_READY | 30 s |
| runtime (post L1_READY) | 15 s heartbeat (1 s tick pets) |

A hung kernel stops feeding the dog, the hardware resets the
machine, and OCBL - running again after the reset - reads the state
area, judges the stall from the stage plus the measured silence
(**8 s** splits a manual warm reset, which measures under ~4 s of
silence, from a real watchdog-caught hang, which measures at least
the runtime budget), records BOOT_FAILED in the reserved area, shows
the failure notice for 4 s, and pulses the 8042 reset.

## SLEEP_ARMED (the wake-failure detector)

The kernel sets the `SLEEP_ARMED` flag immediately before the SLP_EN
write (entering S3/S0ix) and clears it on the resume path.  A reboot
that finds the flag still set means the wake vector crashed or the
machine died mid-sleep: OCBL records WAKE_FAILED and routes OCIRE,
which offers the wake-failure takeover menu.

Next: [troubleshooting and status codes](ocbl-troubleshooting.md).
