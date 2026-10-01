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
} rtc_time_t;

/* Read the RTC, converting BCD and handling the midnight rollover.
 * The returned time is UTC (QEMU/SeaBIOS RTC convention). */
void rtc_read(rtc_time_t *out);

/* Current Unix timestamp (seconds since 1970-01-01 UTC). */
u64 rtc_unix_now(void);

#endif /* OC_RTC_H */
