<!-- SPDX-License-Identifier: Apache-2.0 -->
# 时钟源

| 源 | 驱动 | 命令 | 角色 |
|--------|--------|---------|------|
| PIT | `kernel/core/core_timer.c` | `timer` | THE 节拍：100 Hz，调度器，软定时器 |
| HPET | `drivers/time/driver_hpet.c` | `hpet` | 高精度计数器 |
| ACPI PM timer | `drivers/time/driver_pmtimer.c` | `pmtimer` | 传统后备 |
| TSC | `drivers/time/driver_tsc.c` | `tsc` | 周期计数器 |
| CMOS RTC | `drivers/time/driver_cmos_rtc.c` | `rtc` | 墙上时钟 |

节拍保持为 PIT - 整台机器的一个 10 ms 心跳；其他都是只读精度源（且 `rtc` 用于设置/读取墙上时间）。E2E 定时器套件验证单调性和跨源一致性。
