/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/time L1 surface.
 *
 * Ring-3 dispatch for the time service (SYS_L1SVC cat "time").  The
 * four operations carry the exact command bodies of the v1 oc>
 * commands rtc / hpet / pmtimer / tsc (recovery/shell_cmds_timer.c);
 * only the output sink changed: writes go to the caller-supplied
 * text buffer instead of the screen console, so the ring-3 shell (ush)
 * owns presentation.  Logic, tests and honesty (honest FAIL / honest
 * skip lines) are byte-for-byte the v1 behaviour.
 *
 * Declared through OC_SERVICE_DECL_L1 in .oc_svcs - collected at boot
 * by core_registry_collect(), reachable from ring 3 via SYS_L1SVC(110).
 */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "driver_cmos_rtc.h"
#include "driver_hpet.h"
#include "driver_pmtimer.h"
#include "driver_tsc.h"
#include "driver_acpi.h"
#include "core_rtc.h"
#include "lib_string.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* output-buffer helpers (same names/shapes as the v1 console ones)   */
/* ------------------------------------------------------------------ */
static char *s_out;
static int   s_left;

static void put(const char *s) {
    while (*s && s_left > 1) { *s_out++ = *s++; s_left--; }
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

/* Skip to the next token start (helper for two-word subcommands). */
static const char* skip_word(const char *p) {
    while (*p && *p != ' ' && *p != '\t') p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* |hz - nominal| in basis points (100 bp = 1%). */
static u64 hz_delta_bp(u64 hz, u64 nominal) {
    u64 diff = hz > nominal ? hz - nominal : nominal - hz;
    return diff * 10000ull / nominal;
}

/* ------------------------------------------------------------------ */
/* rtc (L1SVC_TIME_RTC)                                               */
/* ------------------------------------------------------------------ */
static int l1_time_rtc(const char *args) {
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
/* hpet (L1SVC_TIME_HPET)                                             */
/* ------------------------------------------------------------------ */
static int l1_time_hpet(const char *args) {
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

/* ------------------------------------------------------------------ */
/* pmtimer (L1SVC_TIME_PMTIMER)                                       */
/* ------------------------------------------------------------------ */
static int l1_time_pmtimer(const char *args) {
    const char *p = args;
    while (*p == ' ' || *p == '\t') p++;

    if (!driver_pmtimer_available()) {
        put("pmtimer: not available (no FADT PM_TMR_BLK)\n");
        return 1;
    }

    if (word_is(&p, "read")) {
        int loops = 3;
        (void)next_int(&p, &loops);
        if (loops < 1) loops = 3;
        if (loops > 20) loops = 20;
        u32 start = driver_pmtimer_read();
        for (int i = 0; i < loops; i++) {
            put("  raw=");
            put_hex(driver_pmtimer_read(), 8);
            put("\n");
        }
        u32 end = driver_pmtimer_read();
        u32 dticks = driver_pmtimer_delta32(start, end);
        put("delta: ");
        put_u64(dticks);
        put(" ticks");
        if (driver_pmtimer_hz_measured()) {
            put(" = ");
            put_u64(driver_pmtimer_elapsed_ms(start, end));
            put(" ms");
        } else {
            put(" (not calibrated - run 'pmtimer cal')");
        }
        put("\n");
        return 0;
    }
    if (word_is(&p, "cal")) {
        put("PM timer calibration vs PIT (~1.1 s window)\n");
        u64 hz = 0;
        if (driver_pmtimer_calibrate(&hz) != 0) {
            put("calibration: FAILED (no samples)\n");
            return 1;
        }
        u64 nominal = OC_PM_TIMER_NOMINAL_HZ;
        u64 bp = hz_delta_bp(hz, nominal);
        put("samples: c0=");
        put_hex(driver_pmtimer_cal_c0(), 8);
        put(" c1=");
        put_hex(driver_pmtimer_cal_c1(), 8);
        put(" pit_ms=");
        put_u64(driver_pmtimer_cal_ms1() - driver_pmtimer_cal_ms0());
        put("\nmeasured: ");
        put_u64(hz);
        put(" Hz (nominal ");
        put_u64(nominal);
        put(", delta ");
        put_u64(bp);
        put(" bp) ");
        put(bp <= 500 ? "PASS" : "FAIL");
        put("\n");
        return bp <= 500 ? 0 : 1;
    }
    if (word_is(&p, "wrap")) {
        int fails = 0;
        u32 mask = driver_pmtimer_mask();
        int w = driver_pmtimer_config_width();
        put("effective width: ");
        put_dec(w);
        put("bit (mask=0x");
        put_hex(mask, 8);
        put(")\n");
        /* wrap-safe modular delta across a counter wrap (24-bit wrap
         * period is 2^24/3.579545MHz = 4.67 s; 32-bit is ~20 min) */
        u32 s = (w == 32) ? 0xFFFFFFF0u : 0x00FFFFF0u;
        u32 e = 0x0000001Fu;
        u32 d = driver_pmtimer_delta32(s, e);
        put("wrap-safe delta: start=0x");
        put_hex(s, 8);
        put(" end=0x0000001f elapsed=");
        put_u64(d);
        put(d == 47 ? " PASS\n" : " FAIL\n");
        if (d != 47) fails++;
        /* 64-bit extension across a wrap */
        u32 ps = (w == 32) ? 0xFFFFFF00u : 0x00FFFFF0u;
        u64 out64 = 0;
        u64 expect_out = (w == 32) ? 0x100000080ull : 0x1000080ull;
        u64 wraps = driver_pmtimer_extend(ps, 0x00000080u, 0, &out64);
        put("64-bit extension: prev=0x");
        put_hex(ps, 8);
        put(" cur=0x00000080 wraps=");
        put_u64(wraps);
        put(" out=");
        put_hex(out64, 16);
        put((wraps == 1 && out64 == expect_out) ? " PASS\n" : " FAIL\n");
        if (!(wraps == 1 && out64 == expect_out)) fails++;
        /* plain no-wrap delta */
        u32 d2 = driver_pmtimer_delta32(0x00000100u, 0x00000200u);
        put("no-wrap delta: start=0x00000100 end=0x00000200 elapsed=");
        put_u64(d2);
        put(d2 == 256 ? " PASS\n" : " FAIL\n");
        if (d2 != 256) fails++;
        put("pmtimer wrap: ");
        put(fails == 0 ? "PASS" : "FAIL");
        put("\n");
        return fails == 0 ? 0 : 1;
    }
    if (word_is(&p, "test")) {
        int fails = 0;
        u64 hz = 0;
        if (driver_pmtimer_calibrate(&hz) == 0) {
            u64 bp = hz_delta_bp(hz, OC_PM_TIMER_NOMINAL_HZ);
            put("  calibrate vs PIT: ");
            put_u64(hz);
            put(" Hz, delta ");
            put_u64(bp);
            put(" bp: ");
            put(bp <= 500 ? "PASS" : "FAIL");
            put("\n");
            if (bp > 500) fails++;
        } else {
            put("  calibrate vs PIT: FAIL\n");
            fails++;
        }
        u32 r0 = driver_pmtimer_read();
        u32 r1 = driver_pmtimer_read();
        u32 r2 = driver_pmtimer_read();
        int mono = driver_pmtimer_delta32(r0, r1) > 0 &&
                   driver_pmtimer_delta32(r1, r2) > 0;
        put("  monotonic reads: ");
        put(mono ? "PASS" : "FAIL");
        put("\n");
        if (!mono) fails++;
        put("pmtimer: ");
        put(fails == 0 ? "PASS" : "FAIL");
        put("\n");
        return fails == 0 ? 0 : 1;
    }

    /* default: status dump */
    put("PM timer: ");
    put(driver_pmtimer_space() == 1 ? "port=" : "mem=");
    put_hex(driver_pmtimer_base(), 8);
    put(driver_pmtimer_space() == 1 ? " (SystemIO)" : " (SystemMemory)");
    put(" width=");
    put_dec(driver_pmtimer_bit_width());
    put("bit (FADT ");
    put(driver_pmtimer_is_extended() ? "X_PM_TMR" : "pm_tmr_blk");
    put(")  delta mask=");
    put_hex(driver_pmtimer_mask(), 8);
    put(" (");
    put_dec(driver_pmtimer_config_width());
    put("bit)\nnominal: ");
    put_u64(OC_PM_TIMER_NOMINAL_HZ);
    put(" Hz  measured: ");
    put_u64(driver_pmtimer_hz_measured());
    put(driver_pmtimer_hz_measured() ? " Hz\n" : " Hz (not calibrated)\n");
    put("read: ");
    put_hex(driver_pmtimer_read(), 8);
    put("  now64: ");
    put_hex(driver_pmtimer_now64(), 16);
    put("  wraps: ");
    put_u64(driver_pmtimer_wrap_count());
    put("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* tsc (L1SVC_TIME_TSC)                                               */
/* ------------------------------------------------------------------ */
static int l1_time_tsc(const char *args) {
    const char *p = args;
    while (*p == ' ' || *p == '\t') p++;

    if (word_is(&p, "cal")) {
        const char *p2 = skip_word(p);
        if (word_is(&p2, "hpet")) {
            u64 hz = 0;
            if (driver_tsc_calibrate_hpet(&hz) != 0) {
                put("tsc cal hpet: HPET not available (honest FAIL)\n");
                return 1;
            }
            put("TSC calibration vs HPET (~0.1 s): ");
            put_u64(hz);
            put(" Hz\n");
            u64 cached;
            const char *src;
            if (driver_tsc_calibrated(&cached, &src) == 0) {
                u64 bp = hz > cached ? (hz - cached) * 10000ull / cached
                                     : (cached - hz) * 10000ull / cached;
                put("vs cached ");
                put(src);
                put(": delta ");
                put_u64(bp);
                put(" bp\n");
            }
            return 0;
        }
        if (word_is(&p2, "pmtimer")) {
            u64 hz = 0;
            if (driver_tsc_calibrate_pmtimer(&hz) != 0) {
                put("tsc cal pmtimer: PM timer not calibrated (honest FAIL)\n");
                return 1;
            }
            put("TSC calibration vs PM timer (~0.5 s): ");
            put_u64(hz);
            put(" Hz\n");
            u64 cached;
            const char *src;
            if (driver_tsc_calibrated(&cached, &src) == 0) {
                u64 bp = hz > cached ? (hz - cached) * 10000ull / cached
                                     : (cached - hz) * 10000ull / cached;
                put("vs cached ");
                put(src);
                put(": delta ");
                put_u64(bp);
                put(" bp\n");
            }
            return 0;
        }
        /* default PIT calibration, run twice for a consistency check */
        put("TSC calibration vs PIT (~1.1 s window, two runs)\n");
        u64 hz1 = 0, hz2 = 0;
        if (driver_tsc_calibrate_pit(&hz1) != 0 ||
            driver_tsc_calibrate_pit(&hz2) != 0) {
            put("tsc cal: FAILED (no samples)\n");
            return 1;
        }
        u64 hi = hz1 > hz2 ? hz1 : hz2;
        u64 lo = hz1 > hz2 ? hz2 : hz1;
        u64 bp = lo ? (hi - lo) * 10000ull / lo : 0;
        put("run1: ");
        put_u64(hz1);
        put(" Hz  run2: ");
        put_u64(hz2);
        put(" Hz  drift: ");
        put_u64(bp);
        put(" bp\n");
        if (bp > 1000) {
            put("tsc cal: FAIL (drift > 10%)\n");
            return 1;
        }
        /* cache the second run so delay/other users have a frequency */
        driver_tsc_autocalibrate();
        put("tsc cal: PASS (cached ");
        put_u64(hz2);
        put(" Hz via ");
        put(driver_tsc_source());
        put(")\n");
        return 0;
    }
    if (word_is(&p, "delay")) {
        int ms_arg = 0;
        u64 ms = 100, actual = 0;
        if (next_int(&p, &ms_arg) == 0 && ms_arg > 0) ms = (u64)ms_arg;
        if (ms < 1) ms = 100;
        if (ms > 5000) ms = 5000;
        put("TSC busy-wait delay: target ");
        put_u64(ms);
        put(" ms\n");
        if (driver_tsc_delay_ms(ms, &actual) != 0) {
            put("tsc delay: FAILED (no calibration available)\n");
            return 1;
        }
        u64 lo = ms * 75 / 100, hi = ms * 125 / 100;
        put("PIT-measured actual: ");
        put_u64(actual);
        put(" ms ");
        put(actual >= lo && actual <= hi ? "PASS" : "FAIL");
        put("\n");
        return (actual >= lo && actual <= hi) ? 0 : 1;
    }
    if (word_is(&p, "test")) {
        int fails = 0;
        /* monotonicity over a batch of reads */
        int mono = 1;
        u64 prev = driver_tsc_read();
        for (int i = 0; i < 1000; i++) {
            u64 cur = driver_tsc_read();
            if (cur <= prev) { mono = 0; break; }
            prev = cur;
        }
        put("  monotonic (1000 reads): ");
        put(mono ? "PASS" : "FAIL");
        put("\n");
        if (!mono) fails++;
        /* calibration consistency */
        u64 hz1 = 0, hz2 = 0;
        if (driver_tsc_calibrate_pit(&hz1) == 0 &&
            driver_tsc_calibrate_pit(&hz2) == 0) {
            u64 hi = hz1 > hz2 ? hz1 : hz2;
            u64 lo = hz1 > hz2 ? hz2 : hz1;
            u64 bp = lo ? (hi - lo) * 10000ull / lo : 0;
            put("  calibrate 2x PIT: ");
            put_u64(hz1);
            put(" / ");
            put_u64(hz2);
            put(" Hz, drift ");
            put_u64(bp);
            put(" bp: ");
            put(bp <= 1000 ? "PASS" : "FAIL");
            put("\n");
            if (bp > 1000) fails++;
        } else {
            put("  calibrate 2x PIT: FAIL\n");
            fails++;
        }
        driver_tsc_autocalibrate();
        u64 actual = 0;
        if (driver_tsc_delay_ms(100, &actual) == 0) {
            put("  TSC delay 100 ms: PIT-measured ");
            put_u64(actual);
            put(" ms: ");
            put(actual >= 75 && actual <= 125 ? "PASS" : "FAIL");
            put("\n");
            if (actual < 75 || actual > 125) fails++;
        } else {
            put("  TSC delay 100 ms: FAIL (no calibration)\n");
            fails++;
        }
        put("tsc: ");
        put(fails == 0 ? "PASS" : "FAIL");
        put("\n");
        return fails == 0 ? 0 : 1;
    }

    /* default: raw read + invariant flag + cached calibration */
    put("TSC: ");
    put_hex(driver_tsc_read(), 16);
    put(" (serialized rdtsc)\n");
    put("invariant: ");
    put_dec(driver_tsc_invariant());
    put(" (CPUID 0x80000007 EDX[8])\n");
    u64 hz;
    const char *src;
    if (driver_tsc_calibrated(&hz, &src) == 0) {
        put("calibrated: ");
        put_u64(hz);
        put(" Hz via ");
        put(src);
        put("\n");
    } else {
        put("calibrated: not yet (run 'tsc cal')\n");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SYS_L1SVC entry point                                              */
/* ------------------------------------------------------------------ */
int time_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
        case L1SVC_TIME_RTC:     return l1_time_rtc(a);
        case L1SVC_TIME_HPET:    return l1_time_hpet(a);
        case L1SVC_TIME_PMTIMER: return l1_time_pmtimer(a);
        case L1SVC_TIME_TSC:     return l1_time_tsc(a);
        default: return -1;
    }
}

/* v2.0.0 file-local service identity: the category string equals the
 * services/time folder name; the ring-3 surface is time_l1_dispatch.
 * (bring-up stays in kmain - it is order-sensitive: RTC before HPET
 * before the calibrated TSC read used by the boot log timestamps.) */
OC_SERVICE_DECL_L1("time", "time service (RTC/HPET/PM timer/TSC + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   time_l1_dispatch);

