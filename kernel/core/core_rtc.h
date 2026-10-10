/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/rtc.h
 * Purpose: CMOS RTC wall-clock (x86 ports 0x70/0x71) for X.509 validity.
 */
#ifndef OC_RTC_H
#define OC_RTC_H

#include "types.h"

typedef struct {
    int year, month, day, hour, minute, second;
} core_rtc_time_t;

/* BUG-0287 (A3-10): raw CMOS snapshot decoder used by core_rtc_read.
 * Exposed (G8 batch) so the regression suite can drive the REAL
 * production decoder with injected 12/24-hour, BCD/binary snapshots
 * (g8test A group) in addition to live CMOS reads. */
#define OC_G8_RTC_DECODE 1
void core_rtc_decode(u8 second, u8 minute, u8 hour, u8 day, u8 month,
                     u8 year, u8 century, u8 regb, core_rtc_time_t *out);

/* Read the RTC, converting BCD and handling the midnight rollover.
 * The returned time is UTC (QEMU/SeaBIOS RTC convention). */
void core_rtc_read(core_rtc_time_t *out);

/* Current Unix timestamp (seconds since 1970-01-01 UTC). */
u64 core_rtc_unix_now(void);

#endif /* OC_RTC_H */
