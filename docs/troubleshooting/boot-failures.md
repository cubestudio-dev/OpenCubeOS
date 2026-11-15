<!-- SPDX-License-Identifier: Apache-2.0 -->
# Boot failure triage

Four programs touch a boot: **OCBL** (the loader), the **kernel**
(OCOS), the boot-state contract (**OCST**), and **OCIRE** (recovery).
This page walks every failure class in that order, then the A/B slot
logic, then the deliberate-failure tooling (/HANGAT).

## 1. Read the boot log as a checklist

The kernel prints one line per subsystem in bring-up order, formatted
`[HH:MM:SS.mmm] <stage>: OK|FAIL`. Quiet level 1 suppresses non-FAIL
lines; anything containing `FAIL` is a failure for the state judge.
The OCST stage ladder (also what OCBL watches): `BOOT`,
`KM_AIN_ENTERED`, `SCHED_READY`, `DRIVERS_READY`, `SERVICES_READY`,
`L1_LAUNCHED`, `L1_READY`, runtime 255 = `RUNTIME`.

Position-based triage:

1. **Nothing at all on serial** — OCBL didn't run or the ISO entry is
   wrong. Boot the ISO's other entry; try BIOS vs UEFI; capture with
   `-serial file:boot.log`.
2. **Stops before the multiboot2 handshake** — the mb2 handoff failed
   (`[oc] FATAL: bad multiboot2 magic` / `multiboot2 parse failed`) —
   capture serial and check the mbi tag parse; are you booting through
   OCBL/GRUB at all?
3. **`[oc] FATAL: fb_init failed`** — no framebuffer was handed over
   (`[boot] VBE FAILED`/`GOP FAILED` from OCBL precede it). Fix the
   bootloader's video mode.
4. **Stops at a driver line** — that family's probe failed on your
   hardware; the message above it names the device. Remove the device
   (or the QEMU flag) to confirm.
5. **Reaches `L0 ready, waiting for L1` but no prompt** — the launcher
   failed: `l1 launcher: no default L1 app declared` (build damage) or
   `failed to start the default L1 app` (ush broken — boot the ISO and
   run `help_a_test`).
6. **Reboots after the prompt** — panic (policy REBOOT), watchdog, or
   OCBL judged a stall. After the second boot: `crashlog`, `dumpinfo`,
   `dmesg`, `abcfg` (what the boot menu tried), `/var/log/crash.log`.

The boot self-tests already prove: exception path (3/3 counters),
registry count (284), crash recovery armed. If those lines printed
OK, the platform is sound — look at the specific feature, not the
machine.

## 2. OCBL's six-class scanner — when an entry is missing

`ocbl_scan_all()` enumerates drives, gates CD-likes to 2048-byte
sectors, sweeps partitions, then falls back to whole-disk
"superfloppy" detection. Every volume is judged in short-circuit
priority:

| Priority | Class | Detection | Menu label |
|----------|-------|-----------|------------|
| 1 | OCSE | ISO9660 volume carrying `/ocse.mrk` (judged FIRST — an install medium never boots as OCOS even with a kernel on it) | `OpenCubeOS Setup (OCSE)` |
| 2 | OCOS | any volume with `/boot/opencube.elf` whose first 8 KiB contain mb2 magic `0xE85250D6` | `Open Cube OS (slot A\|B, <size> KiB)` |
| 3 | OCIRE | any volume (incl. the 0xEE shadow and ISOs) with `/boot/ocire.elf` | `OCIRE recovery` |
| 4 | WINDOWS | `/EFI/Microsoft/Boot/bootmgfw.efi` | `<volume> ...` |
| 5 | LINUX | ESP with `/EFI/<vendor>/{grubx64,shimx64,systemd-bootx64}.efi` | `<volume> (hd<N> gpt\|p<N>)` |
| 6 | GENERIC | `/EFI/BOOT/BOOTX64.EFI` | `EFI loader` |
| — | SHUTDOWN | synthesized, always last | `Shutdown` |

Declared limit: no ext2/3/4 driver in OCBL — Linux/Windows are
detected via their ESP, never via ext/NTFS roots.

If your volume does not appear:

- The marker file is missing/corrupt (`/boot/opencube.elf` without mb2
  magic registers as nothing). Re-install.
- A volume carrying BOTH `/boot/opencube.elf` and `/boot/ocire.elf`
  exposes the OCIRE entry too (the short-circuit no longer hides it).
- Partitions 2/3 of an A/B disk are "slot-consumed" — they cannot
  register their own entries.
- Serial trace `[scan] entries: <N>` counts what was found.

## 3. OCBL's own failures

- `[boot] FATAL: payload volume remount failed` — /boot remount before
  kernel load; re-install.
- `[boot] FATAL: kernel load failed` — the ELF did not load (missing,
  corrupt, or segments outside `[want_base, 0x40000000)`; the
  overflow-safe bounds are the BUG-46 fix).
- `[efi] FATAL: fixed low-memory windows not ...` — the 0x98000/0x9B000
  OCST islands are occupied by firmware; free low memory or use BIOS.
- `[efi] FATAL: memory map failed` / `ExitBootServices refused` —
  firmware memory-services trouble; retry, update firmware.
- `[chain] VBR read failed` / `LoadImage failed` — the chainload
  target is not bootable (expected for the non-PE stub test disk).
- `[boot] ACPI S5 shutdown` then `[boot] S5 did not power off; halting`
  — board ignores S5.

## 4. The boot-state contract (OCST) and the judge

Two fixed low-memory blocks survive resets:

- **0x98000** (magic `OCS1`): the live stage + heartbeat + kflags —
  the kernel blind-writes it.
- **0x9B000** (magic `OCB1`): the post-mortem record
  `{kind, stall, seq, ts_unix, silence_sec, routed, checksum}` — kind
  BOOT_FAILED or USER_REQUEST.

Budgets: 30 s boot (BOOT→KM_AIN_ENTERED), 10 s per phase, 30 s L1
wait, 15 s runtime heartbeat. Judgment threshold: **≥ 8 s RTC
silence** = failed boot. On a judged failure OCBL writes BOOT_FAILED,
shows `Open Cube OS failed to boot at stage: <STAGE>` (4 s dwell),
pulses the 8042 reset — and the next boot **auto-routes to OCIRE,
one-shot** (`ocbl_statemon_route_pending()`).

Honest non-failures (do not chase these): `previous boot shut down cleanly (seq <n>)`, `warm reset with recent progress (silence <n>s) - not a failure`, `previous boot armed no payload - no failure`. When
no hardware watchdog exists: `BOOT_FAILED record pending ... detection degrades to the next reset`. A frozen RTC: `[rtc] clock frozen - skipping the dwell` (silence unmeasurable — check the RTC battery).

## 5. OCIRE takeover — when recovery happens and what it says

You land in OCIRE when: (a) OCBL judged a BOOT_FAILED (auto-route,
strictly one-shot via the `routed` bit); (b) you picked the OCIRE menu
entry (USER_REQUEST); (c) the supervisor was the disk default
(`ocire.mode=sup`).

OCIRE's first screen explains itself (`ocire_ocbl_report()`):

- `Recovery session requested by the user (seq <n>)` — your menu pick.
- `Previous boot FAILED at stage <STAGE>` + per-stage advice:
  RESET/KM_AIN/SCHED → run `syscheck`; DRIVERS → `syscheck` + review
  drivers; SERVICES → `repair`; L1_LAUNCHED → `syscheck`; L1_READY and
  runtime → `logread` + `repair`.
- `No boot failure was judged - this is a manual recovery session.`

Then the recovery ladder: `syscheck` (10 checks over the volumes —
verdicts like `BAD (magic/state invalid)`, `MISSING/UNREADABLE`,
`DIGEST MISMATCH`), `logread` (the OCBL boot log; `BAD ring header`
→ `repair quick`), `repair` (rebuild from the golden set; the
`Type YES` confirm reformats the system volume). Supervisor-level
failures (`mailbox write failed - skipping the reboot to avoid a reset loop`) mean the mailbox must be repaired before rebooting — that guard
is the BUG-47 fix.

## 6. A/B slots and rollback

The A/B layout on disk: p1 boot/flags (FAT32, 32 MB: `/boot/next_B`,
`/boot/ok_B`, `/boot/bootfail_B`, grub.cfg), p2 slot A (128 MB), p3
slot B (128 MB), p4 /data. Slot selection in `judge_volume()`:

- `next_B` present AND `bootfail_B` absent → boot **B** (partition 3).
- both `next_B` + `bootfail_B` → **rollback to A** (partition 2), menu
  label gains ` rollback` — this is the automatic rollback trigger:
  the B kernel writes `bootfail_B` pessimistically at boot and removes
  it only when the system is fully up.
- `ok_B` present → B; otherwise → A.
- Current slot arrives via the mb2 cmdline `oc.slot=A|B|ISO`; the
  boot-attempt budget lives in `/boot/grub/grubenv` (`ab_attempts`,
  max 3; the kernel resets it when A finishes booting or B confirms).

Commands: `update status` (slot + versions + the `A/B disk: present`
anchor), `abcfg` (the boot config), `rollback` (clears the flags),
`ab_partition_test` (the protocol test). A stale flag making the menu
pick the wrong slot: force the other slot from the OCBL menu, then
`update rollback` from the running system.

## 7. /HANGAT — deliberate stalls and the elimination method

`/HANGAT` is a 1-byte diagnostic file ('1'..'7') on the payload
volume; OCBL silently appends `oc.hang=N` to the kernel cmdline and
the shipped kernel deliberately halts after stage N ('7' = post-
L1_READY runtime hang). Purpose: exercise the whole
watchdog→reset→OCBL-judgment→OCIRE-route chain **from shipped images,
no test-built kernel needed**.

Symptoms when present (and you did not mean it): the kernel halts at a
stage with interrupts off; serial shows `[ocst] /HANGAT diagnostic: will halt after stage <N>` and `[ocst] HANGAT: deliberate halt with interrupts off `; OCBL shows ` (the kernel will halt after the stage)`.

**Elimination method**: if boots keep "failing" at a consistent stage
with clean hardware, check for `/HANGAT` on the payload volume first —
mount the volume from OCIRE or another machine and delete it. A
complete no-op without the file.

## 8. OCSE boot failures (installer medium)

- `[ocse] FATAL: no usable disk - halting` — attach a target disk.
- `ocblmbr.bin MISSING` / `ocos.tar MISSING` — the install medium is
  incomplete: re-download the ISO, verify size/SHA (BUG-107 is the
  historical mismatch).
- `[ocse] STAGE <n> FAILED: <what>` — the installer kernel's own
  bring-up; the mirrored OCOS messages apply.
- The installer step chain and its fixes:
  [error-catalog.md](error-catalog.md) §3.4.

## Related pages

- [error-codes.md](error-codes.md) — panic sites, stage formats
- [error-catalog.md](error-catalog.md) §1-§4 — every boot-side message
- [../manual/boot-modes.md](../manual/boot-modes.md) — the eight boot
  modes
- [../manual/recovery.md](../manual/recovery.md) — OCIRE usage
- [qemu-debugging.md](qemu-debugging.md) — capture, triple-fault
  debugging
