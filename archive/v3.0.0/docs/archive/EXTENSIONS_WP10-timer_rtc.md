<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
# WP-10-timer_rtc — time sources (L0 extension)

Batch: WP-10-timer_rtc (first drop, no suffix).
Scope: the four hardware time sources wired into the L0 kernel and
exercised live in QEMU on both BIOS (SeaBIOS) and UEFI (OVMF) boots:
CMOS RTC (status + IRQ8 interrupts + alarm), HPET (table + MMIO +
comparators), ACPI PM timer (read + wrap + PIT calibration), TSC
(reads + three-reference calibration + delay).

## 1. Domains delivered

1. **CMOS RTC** — `drivers/time/driver_cmos_rtc.c/.h`
   - wall-clock reading stays in `kernel/core/core_rtc.c` (WP-09,
     reused unchanged: BCD conversion, 12/24-hour mode, PM-flag fix)
   - status registers A/B/C/D decoded: UIP, periodic rate index
     (32768>>(rate-1) Hz), PIE/AIE/UIE enables, DM (BCD/binary),
     24-hour flag, VRT, FADT century register
   - IRQ8 counting handler on the shared PIC chain (arch_irq.c)
   - three REAL interrupt tests driven from the shell:
     update-ended (UIE, 1/s), periodic (PIE, rate 3..15) and alarm
     (AIE, time-of-day match with BCD/binary + 12/24h re-encoding);
     every test restores Reg A/B and uninstalls the handler

2. **HPET** — `drivers/time/driver_hpet.c/.h`
   - consumes the ACPI HPET table parsed by `driver_acpi`
     (`driver_acpi_hpet()`); resolves the MMIO base from the table GAS
     (QEMU: 0xfed00000) and reads the live GCAP capabilities
     (revision, timer count, 64-bit counter, legacy route, clock
     period in femtoseconds -> 100 MHz in QEMU)
   - 64-bit main counter read (re-read-high for consistency)
   - one-shot and periodic comparator tests on timer 1 via the
     LEG_RT legacy route to ISA IRQ8 (timer 0 is never enabled: it
     would collide with the PIT on IRQ0); test deadlines are bounded
     by the HPET counter itself because legacy routing gates the PIT
     tick; hardware state restored after every test
   - counter test cross-checked against the PIT ms clock

3. **ACPI PM timer** — `drivers/time/driver_pmtimer.c/.h`
   - resolves PM_TMR_BLK from the FADT (`pm_tmr_blk`, or `X_PM_TMR`
     when the extended fields are valid; QEMU SeaBIOS: 0x608 SystemIO,
     OVMF FADT: 0xb008 SystemIO)
   - 64-bit wrap extension + modular deltas; 24-bit effective mask by
     default (ACPI 1.0 / PIIX4 / QEMU `d & 0xffffff` / Linux
     `ACPI_PM_MASK` convention), `pmtimer.width=32` config key for
     TMR_VAL_EXT hardware (lib_config, `hda.bdl` precedent)
   - frequency calibrated against the PIT (~1.1 s window; the FADT
     carries no frequency); wrap arithmetic self-test with injected
     values (OC_G8_RTC_DECODE pattern)

4. **TSC** — `drivers/time/driver_tsc.c/.h`
   - serialized rdtsc (cpuid fence), invariant-TSC probe
     (CPUID 0x80000007.EDX[8] — honestly 0 under QEMU TCG)
   - three independent calibration references: PIT (~1.1 s, ±1%),
     HPET main counter (~0.1 s at its known frequency), ACPI PM timer
     (~0.5 s, inside one 24-bit wrap); best-reference autocalibration
     (hpet > pmtimer > pit) feeds a cached frequency
   - TSC-driven busy-wait delay with PIT-measured verification

## 2. New shell commands (4; none removed — interface freeze holds)

```
rtc  hpet  pmtimer  tsc
```

- `rtc [uie [sec] | pie [rate] [count] | alarm [+sec]]` — wall clock +
  status registers + the three interrupt tests
- `hpet [counter [n] | oneshot <ms> | periodic <ms> <count> | test]` —
  capabilities + counter + comparator tests
- `pmtimer [read [n] | cal | wrap | test]` — counter reads + PIT
  calibration (prints the raw samples) + wrap arithmetic + combined
- `tsc [cal [hpet|pmtimer] | delay <ms> | test]` — calibration against
  any reference + delay verification + combined

Boot log gains: `CMOS RTC (ports 0x70/0x71, VRT set)`,
`HPET (ACPI table + MMIO caps)`, `ACPI PM timer (FADT PM_TMR_BLK)`;
`shell command system (196 commands)`.

## 3. Extension-interface contract (frozen four-part rule)

Every new capability ships all four parts:

| Part | Where |
|------|-------|
| Header declaration | `drivers/time/driver_cmos_rtc.h`, `driver_hpet.h`, `driver_pmtimer.h`, `driver_tsc.h`, `shell/shell_cmds_timer.h` |
| Comment contract | each declaration carries its semantics (units, error codes, state restore rules) |
| Default implementation | the four driver `.c` files in `drivers/time/`; the PM-timer mask honours `pmtimer.width` from `/etc/opencube.conf` |
| Documentation | this file + `docs/INTERFACES.md` + `docs/CONFIG.md` + `help` descriptions |

The L1 numbered interface set stays CLOSED at 1-138 (this batch, like
WP-10-input_chipset, adds kernel driver APIs + shell commands, not
numbered L1 interfaces).

## 4. Live-boot defects found and fixed by the QEMU gate

1. **acpi_hpet_t was wrong twice** (`drivers/acpi/driver_acpi.h`):
   the struct omitted the 36-byte SDT header (unlike `acpi_fadt_t`,
   so the parser's table-base pointer made every field read garbage
   starting at the signature) AND split the Event Timer Block ID into
   6 bytes instead of one dword (the GAS belongs at offset 40).
   Nothing had consumed the struct before; the HPET MMIO driver is
   its first user.  Live proof: `hpet` read `base=0x205348434f42b401`
   ("BOCHS " OEM bytes) and faulted before the fix.
2. **PM timer 24-bit wrap misread as a backward step**
   (`drivers/time/driver_pmtimer.c`): QEMU 10.0 masks the PM timer
   read to `d & 0xffffff` (hw/acpi/core.c) — the ACPI 1.0 / PIIX4
   24-bit counter, same mask Linux uses (`ACPI_PM_MASK`).  A naive
   mod-2^32 delta turned a 2^24 wrap into a ~3.6 s backward step
   (reproduced: "measured 3,892,813,081 Hz").  Deltas are masked to
   24 bits by default now, correct on both widths for windows below
   4.67 s; `pmtimer.width=32` unmasks for TMR_VAL_EXT hardware.
3. **TSC-vs-HPET calibration spun forever on a frozen counter**
   (`drivers/time/driver_tsc.c`): QEMU boots with GEN ENABLE_CNF
   clear, so the HPET main counter does not advance; the calibration
   now enables the counter for its window and restores the boot state
   (`driver_hpet_ensure_enabled` / `driver_hpet_restore_enabled`).
4. **HPET periodic test deadline too tight** (`driver_hpet.c`): the
   wait deadline did not scale with the fire count (3 fires need
   ~3 periods, not 1); combined `hpet test` flaked at 2/3 before,
   3/3 after.
5. **OVMF FADT uses a different PM base** (test-side note): SeaBIOS
   builds FADT with PM_TMR at 0x608, OVMF at 0xb008 — the E2E check
   accepts any GAS port; the driver never hardcoded it.

## 5. Verification evidence

- Build: 0 errors 0 warnings (`-Werror`).
- WP-10-timer_rtc E2E (`tools/wp10_timer_rtc_e2e.py`): **29/29 on
  BIOS** and **29/29 on UEFI** (`oc-os-evidence/wp10timer/e2e_bios.log`,
  `e2e_uefi.log`), covering every command above plus `help -w`,
  `help -a`, the 196-command boot line and `uname`.
- Full regression (`tools/run_fix4_regression.py`): **28/28**
  (`oc-os-evidence/wp10timer/regression_full.log`), including
  cryptotest, fstest p2, g6_test full chain, l1test G7/G8, and the
  command-count baseline at 196.
- dhtest: **5/5 correctness tests passed** (TCG).
- SSH E2E (`tools/ssh_mainstream_test.py`): **5/5 ALL PASS**.
- HTTPS E2E (`tools/run_fix4_https_e2e.py`): **7/7 checks passed**.
- Key live numbers (QEMU BIOS): RTC UIE 3/3, PIE 32/32, alarm fired;
  HPET counter measured 100,113,820 Hz (nominal 100,000,000), oneshot
  1/1, periodic 3/3; PM timer calibrated 3,576,824 Hz vs nominal
  3,579,545 (delta 7 bp), wrapped-window case 42 bp; TSC 2x PIT runs
  3,171,949,550 / 3,199,350,216 Hz (drift 86 bp), HPET cross-check
  delta 0 bp, TSC delay 100 ms -> PIT-measured 100 ms.

## 6. Honest notes

- The CMOS RTC read path (`core_rtc.c`) is WP-09 code reused
  unchanged; the century register is read from CMOS index 0x48 with a
  2000+year fallback, and the FADT century register (0x32 on QEMU) is
  now surfaced by `rtc` status but not yet wired into that reader.
- HPET interrupts require the LEG_RT legacy route on this PIC-only
  kernel (no IO-APIC redirections yet); while the legacy route is
  armed, the PIT is disconnected from IRQ0, so system tick pauses for
  the duration of the test (max ~1.5 s) and soft timers stall
  accordingly.  Tests restore the boot state unconditionally.
- QEMU's HPET table block_id (0x8086a201) encodes a nonsense
  comparator count (163) — the live GCAP (3 timers) is authoritative
  and `hpet` prints both.
- The PM timer 64-bit extension requires reads less than one wrap
  period apart (4.67 s at 24-bit); `pmtimer now64` is a debug view,
  not a wall-clock source.
- The TSC frequency is NOT stable across TCG migration/pause (virtual
  clock); on real hardware it is.  Invariant-TSC reports honestly 0
  under TCG.
- The regression command-count baseline moved 192 -> 196 (4 new
  commands); the no-removal side of the freeze is covered by `help -a`
  two-way checks in the batch report.
