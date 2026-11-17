<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 时间与时钟 (4)

### hpet

| 字段 | 值 |
|---|---|
| 名称 | hpet |
| 分类 | 时间与时钟 |
| 功能 | HPET 定时器状态 |
| 用法 | `hpet               : HPET status` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `counter`, `oneshot`, `periodic`, `test` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/time/src/svc_time_l1.c` |

**实际执行示例**：

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

| 字段 | 值 |
|---|---|
| 名称 | pmtimer |
| 分类 | 时间与时钟 |
| 功能 | ACPI PM 定时器状态 |
| 用法 | `pmtimer            : PM timer status` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `cal`, `read`, `test`, `wrap` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/time/src/svc_time_l1.c` |

**实际执行示例**：

```
pmtimer
PM timer: port=00000608 (SystemIO) width=32bit (FADT pm_tmr_blk)  delta mask=00ffffff (24bit)
nominal: 3579545 Hz  measured: 0 Hz (not calibrated)
read: 00f313d2  now64: 0000000000f314f4  wraps: 0
ush> 
```

### rtc

| 字段 | 值 |
|---|---|
| 名称 | rtc |
| 分类 | 时间与时钟 |
| 功能 | CMOS RTC 状态 |
| 用法 | `rtc [uie N]        : CMOS RTC status` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `alarm`, `pie` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/time/src/svc_time_l1.c` |

**实际执行示例**：

```
rtc
RTC: 2026-11-17 01:51:43 (UTC, CMOS ports 0x70/0x71)
Status: UIP=0 BCD=1 24h=1 rate=1024Hz
IRQ: PIE=0 AIE=0 UIE=0 VRT=1 FADT century reg=32
Reg A=26 B=02 C=10 D=80
Counters: total=0 update=0 periodic=0 alarm=0
ush> 
```

### tsc

| 字段 | 值 |
|---|---|
| 名称 | tsc |
| 分类 | 时间与时钟 |
| 功能 | TSC 计数器状态 |
| 用法 | `tsc                : TSC status` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `cal`, `delay`, `hpet`, `pmtimer`, `test` |
| 返回码 | -1, 0, 1 |
| 来源 | `ocos/services/time/src/svc_time_l1.c` |

**实际执行示例**：

```
tsc
TSC: 0000000a7e66d8e4 (serialized rdtsc)
invariant: 0 (CPUID 0x80000007 EDX[8])
calibrated: not yet (run 'tsc cal')
ush> 
```
