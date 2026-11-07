<!-- SPDX-License-Identifier: Apache-2.0 -->
# Time sources

| Source | Driver | Command | Role |
|--------|--------|---------|------|
| PIT | `kernel/core/core_timer.c` | `timer` | THE tick: 100 Hz, scheduler, soft timers |
| HPET | `drivers/time/driver_hpet.c` | `hpet` | high-res counter |
| ACPI PM timer | `drivers/time/driver_pmtimer.c` | `pmtimer` | legacy fallback |
| TSC | `drivers/time/driver_tsc.c` | `tsc` | cycle counter |
| CMOS RTC | `drivers/time/driver_cmos_rtc.c` | `rtc` | wall clock |

The tick stays PIT - one 10 ms heartbeat for the whole machine; the
others are read-only precision sources (and `rtc` sets/reads wall
time). The E2E timer suite verifies monotonicity and cross-source
consistency.
