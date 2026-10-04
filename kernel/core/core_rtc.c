/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-09 mainstream batch
 * File: kernel/rtc.c
 * Purpose: Minimal CMOS RTC reader (ports 0x70/0x71) with BCD conversion
 *          and century handling. Used by the X.509 chain validator.
 */
#include "core_rtc.h"

static inline void core_rtc_outb(u16 p, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline u8 core_rtc_inb(u16 p) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

static u8 core_rtc_reg(u8 reg) {
    core_rtc_outb(0x70, reg);
    return core_rtc_inb(0x71);
}

static int core_rtc_update_in_progress(void) {
    return core_rtc_reg(0x0A) & 0x80;
}

/* days since 1970-01-01 for the civil date (Howard Hinnant's algorithm) */
static u64 days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (u64)era * 146097 + doe - 719468;
}

void core_rtc_read(core_rtc_time_t *out) {
    u8 second, minute, hour, day, month, year, century, regb;
    /* wait for a consistent snapshot */
    while (core_rtc_update_in_progress()) ;
    second = core_rtc_reg(0x00);
    minute = core_rtc_reg(0x02);
    hour = core_rtc_reg(0x04);
    day = core_rtc_reg(0x07);
    month = core_rtc_reg(0x08);
    year = core_rtc_reg(0x09);
    century = core_rtc_reg(0x48);   /* ACPI century register (QEMU provides) */
    regb = core_rtc_reg(0x0B);
    /* re-read if the update raced */
    if (core_rtc_update_in_progress() || second != core_rtc_reg(0x00)) {
        while (core_rtc_update_in_progress()) ;
        second = core_rtc_reg(0x00);
        minute = core_rtc_reg(0x02);
        hour = core_rtc_reg(0x04);
        day = core_rtc_reg(0x07);
        month = core_rtc_reg(0x08);
        year = core_rtc_reg(0x09);
        century = core_rtc_reg(0x48);
        regb = core_rtc_reg(0x0B);
    }

    if (!(regb & 0x04)) {
        /* BCD mode */
        #define BCD(v) ((u8)(((v) & 0x0F) + ((v) >> 4) * 10))
        second = BCD(second);
        minute = BCD(minute);
        day = BCD(day);
        month = BCD(month);
        year = BCD(year);
        century = BCD(century);
        hour = (u8)((hour & 0x0F) + ((hour >> 4) * 10));
        #undef BCD
    }
    if (!(regb & 0x02)) {
        /* 12-hour mode */
        hour = (u8)((hour & 0x7F) + ((hour & 0x80) ? 12 : 0));
        hour = (u8)(hour % 24);
    }
    int full_year = (century >= 19 && century <= 21)
                        ? century * 100 + year
                        : 2000 + year;
    out->year = full_year;
    out->month = month;
    out->day = day;
    out->hour = hour;
    out->minute = minute;
    out->second = second;
}

u64 core_rtc_unix_now(void) {
    core_rtc_time_t t;
    core_rtc_read(&t);
    if (t.month < 1) t.month = 1;
    if (t.month > 12) t.month = 12;
    if (t.day < 1) t.day = 1;
    u64 days = days_from_civil(t.year, (unsigned)t.month, (unsigned)t.day);
    return days * 86400ULL
         + (u64)t.hour * 3600 + (u64)t.minute * 60 + (u64)t.second;
}
