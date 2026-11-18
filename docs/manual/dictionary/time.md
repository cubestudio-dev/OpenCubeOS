<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Time and clocks (4)

### hpet

| Field | Value |
|---|---|
| Name | hpet |
| Category | Time and clocks |
| Function | HPET timer status |
| Usage | `hpet               : HPET status` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `counter`, `oneshot`, `periodic`, `test` |
| Return codes | 0, 1 |
| Source | `ocos/services/time/src/svc_time_l1.c` |

**Executed example**:

```
hpet
HPET: base=fed00000 (ACPI table, GAS SystemMemory)
Table: block_id=8086a201 rev=1 comparators=163 counter64=0 legacy=1
Caps: rev=1 timers=3 64bit=1 legacy=1 period=10000000fs freq=100000000Hz
Main counter: 0000000000000000
IRQ count (legacy-route tests): 0
ush> 
```

### pmtimer

| Field | Value |
|---|---|
| Name | pmtimer |
| Category | Time and clocks |
| Function | ACPI PM timer status |
| Usage | `pmtimer            : PM timer status` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `cal`, `read`, `test`, `wrap` |
| Return codes | 0, 1 |
| Source | `ocos/services/time/src/svc_time_l1.c` |

**Executed example**:

```
pmtimer
PM timer: port=00000608 (SystemIO) width=32bit (FADT pm_tmr_blk)  delta mask=00ffffff (24bit)
nominal: 3579545 Hz  measured: 0 Hz (not calibrated)
read: 00e8c8ae  now64: 0000000000e8c9c9  wraps: 0
ush> 
```

### rtc

| Field | Value |
|---|---|
| Name | rtc |
| Category | Time and clocks |
| Function | CMOS RTC status |
| Usage | `rtc [uie N]        : CMOS RTC status` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `alarm`, `pie` |
| Return codes | 0, 1 |
| Source | `ocos/services/time/src/svc_time_l1.c` |

**Executed example**:

```
rtc
RTC: 2026-11-18 16:43:34 (UTC, CMOS ports 0x70/0x71)
Status: UIP=0 BCD=1 24h=1 rate=1024Hz
IRQ: PIE=0 AIE=0 UIE=0 VRT=1 FADT century reg=32
Reg A=26 B=02 C=10 D=80
Counters: total=0 update=0 periodic=0 alarm=0
ush> 
```

### tsc

| Field | Value |
|---|---|
| Name | tsc |
| Category | Time and clocks |
| Function | TSC counter status |
| Usage | `tsc                : TSC status` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `cal`, `delay`, `hpet`, `pmtimer`, `test` |
| Return codes | -1, 0, 1 |
| Source | `ocos/services/time/src/svc_time_l1.c` |

**Executed example**:

```
tsc
TSC: 0000000a838736a2 (serialized rdtsc)
invariant: 0 (CPUID 0x80000007 EDX[8])
calibrated: not yet (run 'tsc cal')
ush> 
```
