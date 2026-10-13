/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: shell/shell_cmds_timer.c
 * Purpose: shell commands for the time-source work package.
 *
 * Group 1 (CMOS RTC) fills the `rtc` command; the HPET / PM timer / TSC
 * commands land with their groups.  Output is plain ASCII via the
 * screen console; numbers via lib_string (u64_to_str/u64_to_hex).
 */
#include "shell_cmds_timer.h"
#include "shell.h"
#include "driver_cmos_rtc.h"
#include "driver_hpet.h"
#include "driver_acpi.h"
#include "core_rtc.h"
#include "screen_console.h"
#include "lib_string.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* tiny output helpers                                                 */
/* ------------------------------------------------------------------ */

static void put(const char *s) {
    screen_console_puts(s);
}
static void put_u64(u64 v) {
    char b[24];
    u64_to_str(v, b);
    put(b);
}
static void put_hex(u64 v, int min_digits) {
    char b[24];
    u64_to_hex(v, b, min_digits);
    put(b);
}
static void put_dec2(int v) {           /* zero-padded 2-digit decimal */
    char b[3];
    b[0] = (char)('0' + (v / 10) % 10);
    b[1] = (char)('0' + v % 10);
    b[2] = 0;
    put(b);
}
static void put_dec(int v) {
    put_u64((u64)(v < 0 ? 0 : v));
}

/* Parse the next whitespace-separated decimal int; 0 on success. */
static int next_int(const char **p, int *out) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == 0) return -1;
    int v = 0, got = 0;
    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        if (v > 1000000) v = 1000000;   /* clamp, keep parsing */
        (*p)++;
        got = 1;
    }
    if (!got) return -1;
    *out = v;
    return 0;
}

/* Case-insensitive word compare bounded by the word length. */
static int word_is(const char **p, const char *w) {
    const char *s = *p;
    while (*s && *w) {
        char a = *s, b = *w;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        s++; w++;
    }
    if (*w) return 0;
    *p = s;
    return 1;
}

/* ------------------------------------------------------------------ */
/* rtc                                                                 */
/* ------------------------------------------------------------------ */

int shell_cmd_rtc(const char *args) {
    const char *p = args;
    while (*p == ' ' || *p == '\t') p++;

    if (word_is(&p, "uie")) {
        int sec = 3;
        (void)next_int(&p, &sec);
        if (sec < 1) sec = 3;
        put("RTC update-ended interrupt test: ");
        put_dec(sec);
        put(" fires expected (UIE, IRQ8)\n");
        int got = driver_cmos_rtc_uie_test(sec, (u64)sec * 1000 + 4000);
        put("update-ended interrupts: ");
        put_dec(got);
        put(" (expected ");
        put_dec(sec);
        put(") ");
        put(got == sec ? "PASS" : "FAIL");
        put("\n");
        return got == sec ? 0 : 1;
    }
    if (word_is(&p, "pie")) {
        int rate = 10, count = 32;
        if (next_int(&p, &rate) != 0) { rate = 10; }
        if (next_int(&p, &count) != 0) { count = 32; }
        int hz = (rate >= 3 && rate <= 15) ? (32768 >> (rate - 1)) : 0;
        if (hz == 0 || count < 1 || count > 100000) {
            put("rtc pie: invalid rate/count (rate 3..15, count 1..100000)\n");
            return 1;
        }
        put("RTC periodic interrupt test: rate index ");
        put_dec(rate);
        put(" = ");
        put_dec(hz);
        put(" Hz, ");
        put_dec(count);
        put(" fires expected (PIE, IRQ8)\n");
        int got = driver_cmos_rtc_pie_test(rate, count,
                                           (u64)count * 1000 / hz + 5000);
        put("periodic interrupts: ");
        put_dec(got);
        put(" (expected ");
        put_dec(count);
        put(") ");
        put(got == count ? "PASS" : "FAIL");
        put("\n");
        return got == count ? 0 : 1;
    }
    if (word_is(&p, "alarm")) {
        int ahead = 3;
        (void)next_int(&p, &ahead);
        if (ahead < 1) ahead = 3;
        core_rtc_time_t now;
        core_rtc_read(&now);
        int s = now.second + ahead, m = now.minute, h = now.hour;
        while (s >= 60) { s -= 60; m++; }
        while (m >= 60) { m -= 60; h++; }
        put("RTC alarm interrupt test: target ");
        put_dec2(h); put(":"); put_dec2(m); put(":"); put_dec2(s);
        put(" (");
        put_dec(ahead);
        put("s ahead, AIE, IRQ8)\n");
        if (h >= 24) {
            put("alarm target crosses midnight: skipped honestly\n");
            return 1;
        }
        int rc = driver_cmos_rtc_alarm_test(ahead, (u64)ahead * 1000 + 4000);
        if (rc < 0) {
            put("alarm test: internal error\n");
            return 1;
        }
        put("alarm interrupt: ");
        put(rc == 1 ? "fired PASS" : "TIMEOUT FAIL");
        put("\n");
        return rc == 1 ? 0 : 1;
    }

    /* default: wall clock + status registers + interrupt counters */
    if (!driver_cmos_rtc_available()) {
        put("rtc: not probed (driver_cmos_rtc_init did not run)\n");
        return 1;
    }
    core_rtc_time_t t;
    core_rtc_read(&t);
    put("RTC: ");
    put_dec(t.year); put("-");
    put_dec2(t.month); put("-");
    put_dec2(t.day); put(" ");
    put_dec2(t.hour); put(":");
    put_dec2(t.minute); put(":");
    put_dec2(t.second);
    put(" (UTC, CMOS ports 0x70/0x71)\n");

    driver_cmos_rtc_status_t st;
    driver_cmos_rtc_status(&st);
    put("Status: UIP=");
    put_dec(st.uip);
    put(" BCD=");
    put_dec(st.bcd);
    put(" 24h=");
    put_dec(st.hour24);
    put(" rate=");
    put_dec(st.rate_hz);
    put("Hz\nIRQ: PIE=");
    put_dec(st.pie);
    put(" AIE=");
    put_dec(st.aie);
    put(" UIE=");
    put_dec(st.uie);
    put(" VRT=");
    put_dec(st.vrt);
    put(" FADT century reg=");
    put_hex(st.fadt_century, 2);
    put("\nReg A=");
    put_hex(st.reg_a, 2);
    put(" B=");
    put_hex(st.reg_b, 2);
    put(" C=");
    put_hex(st.reg_c, 2);
    put(" D=");
    put_hex(st.reg_d, 2);
    put("\nCounters: total=");
    put_u64(driver_cmos_rtc_irq_total());
    put(" update=");
    put_u64(driver_cmos_rtc_irq_update());
    put(" periodic=");
    put_u64(driver_cmos_rtc_irq_periodic());
    put(" alarm=");
    put_u64(driver_cmos_rtc_irq_alarm());
    put("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* hpet                                                                */
/* ------------------------------------------------------------------ */

int shell_cmd_hpet(const char *args) {
    const char *p = args;
    while (*p == ' ' || *p == '\t') p++;

    if (!driver_hpet_available()) {
        put("hpet: not available (no usable ACPI HPET table / GAS)\n");
        return 1;
    }

    if (word_is(&p, "counter")) {
        int loops = 5;
        (void)next_int(&p, &loops);
        if (loops < 1) loops = 5;
        if (loops > 20) loops = 20;
        put("HPET counter reads (ticks):\n");
        for (int i = 0; i < loops; i++) {
            put("  ");
            put_hex(driver_hpet_counter_read(), 16);
            put("\n");
        }
        u64 dticks = 0, ms = 0;
        if (driver_hpet_counter_test(&dticks, &ms) != 0) {
            put("counter test: FAIL (non-monotonic)\n");
            return 1;
        }
        u64 hz = driver_hpet_freq_hz();
        u64 measured = ms > 0 ? dticks * 1000ull / ms : 0;
        put("counter test: delta=");
        put_u64(dticks);
        put(" ticks in ");
        put_u64(ms);
        put(" ms -> measured ");
        put_u64(measured);
        put(" Hz (nominal ");
        put_u64(hz);
        put(") PASS\n");
        return 0;
    }
    if (word_is(&p, "oneshot")) {
        int ms = 100;
        (void)next_int(&p, &ms);
        if (ms < 1) ms = 100;
        if (ms > 2000) ms = 2000;
        if (!driver_hpet_legacy_cap()) {
            put("hpet: legacy route not available (honest skip)\n");
            return 1;
        }
        put("HPET one-shot test: timer1 -> legacy IRQ8, ");
        put_dec(ms);
        put(" ms\n");
        int got = driver_hpet_oneshot_test(ms);
        if (got < 0) {
            put("oneshot: arm failed (honest FAIL)\n");
            return 1;
        }
        put("oneshot: fired ");
        put_dec(got);
        put(" (expected 1) ");
        put(got == 1 ? "PASS" : "FAIL");
        put("\n");
        return got == 1 ? 0 : 1;
    }
    if (word_is(&p, "periodic")) {
        int ms = 100, count = 3;
        if (next_int(&p, &ms) != 0) ms = 100;
        if (next_int(&p, &count) != 0) count = 3;
        if (ms < 1) ms = 100;
        if (count < 1) count = 3;
        if (!driver_hpet_legacy_cap()) {
            put("hpet: legacy route not available (honest skip)\n");
            return 1;
        }
        put("HPET periodic test: timer1 -> legacy IRQ8, ");
        put_dec(ms);
        put(" ms x ");
        put_dec(count);
        put("\n");
        int got = driver_hpet_periodic_test(ms, count);
        if (got < 0) {
            put("periodic: arm failed or periodic cap missing (honest FAIL)\n");
            return 1;
        }
        put("periodic: ");
        put_dec(got);
        put("/");
        put_dec(count);
        put(got == count ? " PASS" : " FAIL");
        put("\n");
        return got == count ? 0 : 1;
    }
    if (word_is(&p, "test")) {
        int fails = 0;
        u64 dticks = 0, ms = 0;
        if (driver_hpet_counter_test(&dticks, &ms) != 0) {
            put("  counter monotonic: FAIL\n");
            fails++;
        } else {
            u64 hz = driver_hpet_freq_hz();
            u64 measured = ms > 0 ? dticks * 1000ull / ms : 0;
            u64 lo = hz / 2, hi = hz * 2;
            put("  counter monotonic: PASS (measured ");
            put_u64(measured);
            put(" Hz, nominal ");
            put_u64(hz);
            put(")\n");
            if (measured < lo || measured > hi) {
                put("  counter frequency out of range: FAIL\n");
                fails++;
            }
        }
        int g1 = driver_hpet_oneshot_test(100);
        if (g1 == 1) {
            put("  oneshot 100 ms: PASS\n");
        } else {
            put("  oneshot 100 ms: FAIL\n");
            fails++;
        }
        int g2 = driver_hpet_periodic_test(100, 3);
        if (g2 == 3) {
            put("  periodic 100 ms x3: PASS\n");
        } else {
            put("  periodic 100 ms x3: FAIL (got ");
            put_dec(g2);
            put(")\n");
            fails++;
        }
        put("hpet: ");
        put(fails == 0 ? "PASS" : "FAIL");
        put("\n");
        return fails == 0 ? 0 : 1;
    }

    /* default: capability + status dump */
    put("HPET: base=");
    put_hex(driver_hpet_base(), 8);
    put(" (ACPI table, GAS SystemMemory)\n");
    {
        u32 id = driver_hpet_table_block_id();
        put("Table: block_id=");
        put_hex(id, 8);
        put(" rev=");
        put_dec(ACPI_HPET_HW_REV(id));
        put(" comparators=");
        put_dec(ACPI_HPET_COMPARATORS(id));
        put(" counter64=");
        put_dec(ACPI_HPET_COUNTER_64(id));
        put(" legacy=");
        put_dec(ACPI_HPET_LEGACY(id));
        put("\n");
    }
    put("Caps: rev=");
    put_dec(driver_hpet_revision());
    put(" timers=");
    put_dec(driver_hpet_timer_count());
    put(" 64bit=");
    put_dec(driver_hpet_counter_64bit());
    put(" legacy=");
    put_dec(driver_hpet_legacy_cap());
    put(" period=");
    put_u64(driver_hpet_period_fs());
    put("fs freq=");
    put_u64(driver_hpet_freq_hz());
    put("Hz\nMain counter: ");
    put_hex(driver_hpet_counter_read(), 16);
    put("\nIRQ count (legacy-route tests): ");
    put_u64(driver_hpet_irq_count());
    put("\n");
    return 0;
}


void shell_cmds_timer_register(void) {
    shell_register_command_ex("rtc", shell_cmd_rtc,
        "CMOS RTC: time + status + UIE/PIE/alarm interrupt tests",
        "WP-10-timer_rtc");
    shell_register_command_ex("hpet", shell_cmd_hpet,
        "HPET: caps + counter + one-shot/periodic IRQ8 tests",
        "WP-10-timer_rtc");
}
