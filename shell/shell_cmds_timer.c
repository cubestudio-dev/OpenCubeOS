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
/* registration                                                        */
/* ------------------------------------------------------------------ */

void shell_cmds_timer_register(void) {
    shell_register_command_ex("rtc", shell_cmd_rtc,
        "CMOS RTC: time + status + UIE/PIE/alarm interrupt tests",
        "WP-10-timer_rtc");
}
