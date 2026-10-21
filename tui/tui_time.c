/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support-fix4
 * File: tui/tui_time.c
 * Purpose: Time TUI (fix4 4.2.1): view the wall clock, set the RTC
 * (real CMOS write through ports 0x70/0x71), configure the timezone
 * offset (persisted via lib_config, shared with the shell) and report
 * the time-source status (RTC / HPET / PM timer / TSC) plus an honest
 * NTP status. Pages register through the fix4 L1 extension layer so
 * any other module can list/extend them the same way.
 *
 * shell parity: the same core_rtc_read data the `date` command shows
 * and the same lib_config keys (`tz.offset`, `tz.name`) the shell
 * exposes - one source of truth, two frontends.
 */
#include "tui.h"
#include "tui_desktop.h"   /* WP-10-tui-native: tui_tr bilingual pages */
#include "../kernel/lib/lib_string.h"
#include "../kernel/lib/lib_config.h"
#include "../kernel/core/core_rtc.h"
#include "../kernel/core/core_timer.h"
#include "../drivers/time/driver_cmos_rtc.h"
#include "../drivers/time/driver_hpet.h"
#include "../drivers/time/driver_pmtimer.h"
#include "../drivers/time/driver_tsc.h"

/* ---- helpers --------------------------------------------------------- */

static int ti_atoi(const char *s) {
    if (!s) return 0;
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static void fmt2(char *dst, int v) {      /* 00..99, dst >= 3 bytes */
    dst[0] = (char)('0' + (v / 10) % 10);
    dst[1] = (char)('0' + v % 10);
    dst[2] = 0;
}

/* persisted timezone offset in minutes (-720..+840), default UTC */
static int time_tz_offset(void) {
    char v[12];
    lib_config_read_default("tz.offset", v, (int)sizeof(v), "0");
    int neg = 0, i = 0;
    if (v[0] == '-') { neg = 1; i = 1; }
    int m = 0;
    for (; v[i]; i++) {
        if (v[i] < '0' || v[i] > '9') return 0;
        m = m * 10 + (v[i] - '0');
        if (m > 5000) return 0;
    }
    m = (m > 840) ? 840 : m;
    return neg ? -m : m;
}

static void time_tz_offset_set(int minutes) {
    if (minutes < -720) minutes = -720;
    if (minutes > 840) minutes = 840;
    char v[12];
    int a = minutes < 0 ? -minutes : minutes;
    if (minutes < 0) { v[0] = '-'; u64_to_str((u64)a, v + 1); }
    else u64_to_str((u64)a, v);
    lib_config_write("tz.offset", v);
}

/* local time = UTC + offset via Unix-seconds round trip */
static void time_apply_offset(const core_rtc_time_t *utc, int off_min,
                              core_rtc_time_t *out) {
    int y = utc->year;
    long days;
    {
        int yy = y - (utc->month <= 2 ? 1 : 0);
        long era = yy / 400;
        long yoe = yy - era * 400;
        long doy = (153 * (utc->month + (utc->month > 2 ? -3 : 9)) + 2) / 5 + utc->day - 1;
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + doe - 719468;
    }
    long secs = days * 86400L + utc->hour * 3600L + utc->minute * 60L + utc->second;
    secs += (long)off_min * 60L;
    if (secs < 0) secs = 0;
    long z = secs / 86400L; secs -= z * 86400L;
    long h = secs / 3600; secs -= h * 3600;
    long mi = secs / 60; secs -= mi * 60;
    long s = secs;
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy2 = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long d = doy - (153 * mp + 2) / 5 + 1;
    long m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) yy2 += 1;
    out->year = (int)yy2; out->month = (int)m; out->day = (int)d;
    out->hour = (int)h; out->minute = (int)mi; out->second = (int)s;
}

/* ---- CMOS RTC write (real ports, guarded by RegB SET) ---------------- */

static void cmos_write_reg(u8 reg, u8 val) {
    __asm__ volatile("outb %0, %1" :: "a"(reg), "Nd"(0x70));
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(0x71));
}

static u8 cmos_read_reg(u8 reg) {
    u8 v;
    __asm__ volatile("outb %0, %1" :: "a"(reg), "Nd"(0x70));
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(0x71));
    return v;
}

static int rtc_write_time(const core_rtc_time_t *t) {
    if (!t) return -1;
    if (t->year < 1980 || t->year > 2099) return -2;
    if (t->month < 1 || t->month > 12) return -2;
    if (t->day < 1 || t->day > 31) return -2;
    if (t->hour > 23 || t->minute > 59 || t->second > 59) return -2;
    u8 regb = driver_cmos_rtc_read_reg(0x0B);
    int bcd = !(regb & 0x04);         /* RegB DM=0 -> BCD mode */

    /* stop updates, force 24-hour mode for the write */
    cmos_write_reg(0x0B, (u8)((regb | 0x80) & ~0x02));
    u8 s = (u8)t->second, mi = (u8)t->minute, h = (u8)t->hour;
    u8 d = (u8)t->day, mo = (u8)t->month, y = (u8)(t->year % 100);
    u8 ce = (u8)(t->year / 100);
    if (bcd) {
        #define TOBCD(v) (u8)((((v) / 10) << 4) | ((v) % 10))
        s = TOBCD(s); mi = TOBCD(mi); h = TOBCD(h);
        d = TOBCD(d); mo = TOBCD(mo); y = TOBCD(y); ce = TOBCD(ce);
        #undef TOBCD
    }
    cmos_write_reg(0x00, s);
    cmos_write_reg(0x02, mi);
    cmos_write_reg(0x04, h);
    cmos_write_reg(0x07, d);
    cmos_write_reg(0x08, mo);
    cmos_write_reg(0x09, y);
    cmos_write_reg(0x32, ce);
    /* Read back INSIDE the SET window: this proves the CMOS registers
     * took the values (on real MC146818 hardware the clock then counts
     * from the new time). Under QEMU the virtual RTC re-derives the
     * registers from host time once SET clears, so a post-SET read
     * would show host time - an emulator limitation, not a driver
     * one; recorded in docs/KNOWN_ISSUES. */
    int ok = cmos_read_reg(0x04) == h && cmos_read_reg(0x02) == mi &&
             cmos_read_reg(0x00) == s;
    /* resume updates. Keep 24-hour mode: the values written above are
     * 24-hour (restoring the old RegB would leave 12h=0 and the
     * 0x15-hour value would decode as garbage 03:xx afterwards). */
    cmos_write_reg(0x0B, (u8)((regb | 0x02) & ~0x80));
    return ok ? 0 : -3;
}

/* ---- page: clock view ------------------------------------------------- */

static int page_clock_render(tui_window_t *win, int scroll) {
    (void)win;
    core_rtc_time_t utc, local;
    core_rtc_read(&utc);
    int off = time_tz_offset();
    time_apply_offset(&utc, off, &local);
    char b[100], n[16], t[4];
    int row = 1 - scroll;
    if (row++ >= 1) { tui_print_at(row++, 2, tui_tr("Clock (read live from the CMOS RTC)",
                                 "时钟（实时读取 CMOS RTC）")); }
    if (row >= 1) {
        b[0] = 0;
        u64_to_str((u64)utc.year, n); strcat(b, n); strcat(b, "-");
        fmt2(t, utc.month); strcat(b, t); strcat(b, "-");
        fmt2(t, utc.day);   strcat(b, t); strcat(b, "  ");
        fmt2(t, utc.hour);  strcat(b, t); strcat(b, ":");
        fmt2(t, utc.minute);strcat(b, t); strcat(b, ":");
        fmt2(t, utc.second);strcat(b, t);
        strcat(b, "  UTC");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        b[0] = 0;
        u64_to_str((u64)local.year, n); strcat(b, n); strcat(b, "-");
        fmt2(t, local.month); strcat(b, t); strcat(b, "-");
        fmt2(t, local.day);   strcat(b, t); strcat(b, "  ");
        fmt2(t, local.hour);  strcat(b, t); strcat(b, ":");
        fmt2(t, local.minute);strcat(b, t); strcat(b, ":");
        fmt2(t, local.second);strcat(b, t);
        strcat(b, "  UTC+");
        if (off < 0) { strcpy(b + strlen(b) - 5, "  UTC-"); }
        char hh[4];
        fmt2(hh, (off < 0 ? -off : off) / 60);
        strcat(b, hh); strcat(b, ":");
        fmt2(hh, (off < 0 ? -off : off) % 60);
        strcat(b, hh);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        b[0] = 0; strcat(b, tui_tr("Unix seconds: ", "Unix 秒数："));
        u64_to_str(core_rtc_unix_now(), n); strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        b[0] = 0; strcat(b, tui_tr("Timezone: ", "时区："));
        u64_to_str((u64)(off < 0 ? -off : off), n);
        strcat(b, off < 0 ? "UTC-" : "UTC+");
        if (off < 0) strcat(b, n);
        else { char m[4]; fmt2(m, off / 60); strcat(b, m); strcat(b, ":"); fmt2(m, off % 60); strcat(b, m); }
        strcat(b, tui_tr("  (Tab -> Timezone page to change)",
                       "  （Tab -> 时区页可修改）"));
        tui_print_at(row++, 2, b);
    }
    row++;
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Time sources", "时间源"));
    if (row >= 1) {
        int hpet = driver_hpet_available();
        int pmt  = driver_pmtimer_available();
        u64 hz = 0; const char *src = 0;
        int cal = driver_tsc_calibrated(&hz, &src);
        (void)cal;
        b[0] = 0; strcat(b, tui_tr("  RTC (CMOS, ports 0x70/71): active",
                                   "  RTC（CMOS，端口 0x70/71）：活动"));
        tui_print_at(row++, 2, b);
        if (row >= 1) {
            b[0] = 0; strcat(b, tui_tr("  HPET: ", "  HPET："));
            strcat(b, hpet ? tui_tr("available", "可用") : tui_tr("not available", "不可用"));
            if (hpet) {
                char p[24];
                u64_to_str((u64)driver_hpet_period_fs(), p);
                strcat(b, tui_tr("  (period ", "  （周期 ")); strcat(b, p); strcat(b, tui_tr(" fs)", " 飞秒）"));
            }
            tui_print_at(row++, 2, b);
        }
        if (row >= 1) {
            b[0] = 0; strcat(b, tui_tr("  PM timer: ", "  PM 定时器："));
            strcat(b, pmt ? tui_tr("available", "可用") : tui_tr("not available", "不可用"));
            tui_print_at(row++, 2, b);
        }
        if (row >= 1) {
            b[0] = 0; strcat(b, tui_tr("  TSC: ", "  TSC："));
            strcat(b, driver_tsc_invariant() ? tui_tr("invariant", "不变") : tui_tr("variant", "可变"));
            strcat(b, tui_tr(", calibrated: ", "，校准："));
            if (src) { strcat(b, src); }
            else strcat(b, tui_tr("no", "未校准"));
            if (hz) {
                char p[24];
                u64_to_str(hz, p);
                strcat(b, tui_tr(" (", "（")); strcat(b, p); strcat(b, tui_tr(" Hz)", " Hz）"));
            }
            tui_print_at(row++, 2, b);
        }
        if (row >= 1) {
            tui_print_at(row++, 2, tui_tr("  NTP: not built (no NTP client in this image)",
                                        "  NTP：未构建（本镜像无 NTP 客户端）"));
        }
        row++;
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr(
        "Keys: Tab pages | Up/Down scroll | F5 refresh | Esc back",
        "按键：Tab 切页 | 上/下 滚动 | F5 刷新 | Esc 返回"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr(
        "Set the clock: Tab -> 'Set clock' page",
        "设置时钟：Tab -> “设置时钟”页"));
    return row + scroll - 1;
}

/* ---- page: set clock form --------------------------------------------- */

static tui_form_t *g_time_form;
static tui_field_t g_time_fields[6];

static void time_form_init(tui_window_t *win) {
    /* fix4: seed the fields from the RTC exactly ONCE per form window;
     * re-seeding on every repaint threw away whatever the user had
     * typed (the backspace edits vanished on the next F5/scroll
     * repaint). A fresh session gets a fresh window pointer, which
     * invalidates the cached form below. */
    if (g_time_form && g_time_form->win != win) {
        tui_form_free(g_time_form);
        g_time_form = (void *)0;
    }
    if (!g_time_form) {
        core_rtc_time_t utc;
        core_rtc_read(&utc);
        char v[8]; char t[4];
        g_time_fields[0].y = 2; g_time_fields[0].x = 18; g_time_fields[0].width = 4;
        g_time_fields[0].label = tui_tr("Year  ", "年 ");
        g_time_fields[1].y = 3; g_time_fields[1].x = 18; g_time_fields[1].width = 2;
        g_time_fields[1].label = tui_tr("Month ", "月 ");
        g_time_fields[2].y = 4; g_time_fields[2].x = 18; g_time_fields[2].width = 2;
        g_time_fields[2].label = tui_tr("Day   ", "日 ");
        g_time_fields[3].y = 5; g_time_fields[3].x = 18; g_time_fields[3].width = 2;
        g_time_fields[3].label = tui_tr("Hour  ", "时 ");
        g_time_fields[4].y = 6; g_time_fields[4].x = 18; g_time_fields[4].width = 2;
        g_time_fields[4].label = tui_tr("Min   ", "分 ");
        g_time_fields[5].y = 7; g_time_fields[5].x = 18; g_time_fields[5].width = 2;
        g_time_fields[5].label = tui_tr("Sec   ", "秒 ");
        for (int i = 0; i < 6; i++) {
            g_time_fields[i].editable = 1;
            g_time_fields[i].buffer[0] = 0;
            g_time_fields[i].len = 0;
            g_time_fields[i].cursor = 0;
        }
        g_time_form = tui_form_new(win, g_time_fields, 6);
        if (!g_time_form) return;
        u64_to_str((u64)utc.year, v); tui_form_set_field(g_time_form, 0, v);
        fmt2(t, utc.month); tui_form_set_field(g_time_form, 1, t);
        fmt2(t, utc.day);   tui_form_set_field(g_time_form, 2, t);
        fmt2(t, utc.hour);  tui_form_set_field(g_time_form, 3, t);
        fmt2(t, utc.minute);tui_form_set_field(g_time_form, 4, t);
        fmt2(t, utc.second);tui_form_set_field(g_time_form, 5, t);
    }
    tui_form_post(g_time_form);
    tui_status_line(tui_tr("Y-M-D H:M:S, then Enter on Sec to write the CMOS RTC",
                          "年-月-日 时:分:秒，在“秒”上按 Enter 写入 CMOS RTC"));
}

static int page_set_render(tui_window_t *win, int scroll) {
    (void)win;
    (void)scroll;
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_print_at(1, 2, tui_tr(
        "Write the CMOS RTC (UTC). Enter on the last field applies.",
        "写入 CMOS RTC（UTC）。在最后一格按 Enter 生效。"));
    time_form_init(win);
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
    return 8;
}

static int page_set_input(int key) {
    if (!g_time_form) return 0;
    int d = tui_form_driver(g_time_form, key);
    if (d == -1) {
        /* Esc: hand the key back - the generic loop treats it as
         * "leave the page application" (back to the shell). */
        return 0;
    }
    if (d == 1) {
        /* submitted: parse and write */
        core_rtc_time_t t;
        const char *v;
        v = tui_form_field_buffer(g_time_form, 0); t.year = ti_atoi(v);
        v = tui_form_field_buffer(g_time_form, 1); t.month = ti_atoi(v);
        v = tui_form_field_buffer(g_time_form, 2); t.day = ti_atoi(v);
        v = tui_form_field_buffer(g_time_form, 3); t.hour = ti_atoi(v);
        v = tui_form_field_buffer(g_time_form, 4); t.minute = ti_atoi(v);
        v = tui_form_field_buffer(g_time_form, 5); t.second = ti_atoi(v);
        int rc = rtc_write_time(&t);
        if (rc == 0) {
            tui_status_line(tui_tr(
                "CMOS registers written (SET-window readback ok); "
                "real HW counts from the new time, QEMU keeps host time",
                "CMOS 寄存器已写入（SET 窗口回读一致）；真机从新时间计数，"
                "QEMU 保持宿主时间"));
        } else if (rc == -2) tui_status_line(tui_tr(
                "value out of range (1980-2099, valid M/D/H/M/S)",
                "数值超范围（1980-2099，有效的月/日/时/分/秒）"));
        else tui_status_line(tui_tr("RTC write failed (SET-window readback mismatch)",
                              "RTC 写入失败（SET 窗口回读不一致）"));
        return 1;
    }
    /* form nav/edit keys (Tab/arrows/printables/backspace) belong to
     * the form: swallow them so the generic loop does not ALSO treat
     * Tab as "next page" (the double-action jumped straight past the
     * form). F1/F2/F5/Esc still reach the generic loop. */
    if (key == TUI_KEY_TAB || key == TUI_KEY_UP || key == TUI_KEY_DOWN ||
        key == TUI_KEY_BACKSPACE || (key >= 0x20 && key < 0x7F))
        return 1;
    return 0;
}

/* ---- page: timezone ---------------------------------------------------- */

static int page_tz_render(tui_window_t *win, int scroll) {
    (void)win;
    int off = time_tz_offset();
    char b[100], n[16];
    int row = 1 - scroll;
    if (row >= 1) tui_print_at(row++, 2, tui_tr(
        "Timezone (persisted as tz.offset in /etc/opencube.conf)",
        "时区（以 tz.offset 持久化到 /etc/opencube.conf）"));
    if (row >= 1) {
        b[0] = 0; strcat(b, tui_tr("Current offset: ", "当前偏移："));
        u64_to_str((u64)(off < 0 ? -off : off), n);
        if (off < 0) strcat(b, "UTC-"); else strcat(b, "UTC+");
        if (off < 0) { strcat(b, n); }
        else { char m[4]; fmt2(m, off / 60); strcat(b, m); strcat(b, ":"); fmt2(m, off % 60); strcat(b, m); }
        strcat(b, tui_tr("   (", "   （")); u64_to_str((u64)off, n); strcat(b, n); strcat(b, tui_tr(" minutes)", " 分钟）"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr(
        "Keys: +/- one hour, [/] 15 min, s save, r reset to UTC",
        "按键：+/- 一小时，[/] 15 分钟，s 保存，r 重置为 UTC"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr(
        "Examples: Shanghai=UTC+8, Berlin=UTC+1, New York=UTC-5",
        "示例：上海=UTC+8，柏林=UTC+1，纽约=UTC-5"));
    return row + scroll - 1;
}

static int page_tz_input(int key) {
    int off = time_tz_offset();
    if (key == '+') { time_tz_offset_set(off + 60); tui_status_line(tui_tr("timezone +1h (in memory, 's' persists)",
                                                    "时区 +1 小时（内存中，按 s 持久化）")); return 1; }
    if (key == '-') { time_tz_offset_set(off - 60); tui_status_line(tui_tr("timezone -1h (in memory, 's' persists)",
                                                    "时区 -1 小时（内存中，按 s 持久化）")); return 1; }
    if (key == '[') { time_tz_offset_set(off - 15); return 1; }
    if (key == ']') { time_tz_offset_set(off + 15); return 1; }
    if (key == 'r') { time_tz_offset_set(0); tui_status_line(tui_tr("timezone reset to UTC (in memory)",
                                                    "时区已重置为 UTC（内存中）")); return 1; }
    if (key == 's') {
        char v[12];
        int a = off < 0 ? -off : off;
        if (off < 0) { v[0] = '-'; u64_to_str((u64)a, v + 1); }
        else u64_to_str((u64)a, v);
        lib_config_write("tz.offset", v);
        tui_status_line(tui_tr("timezone persisted to /etc/opencube.conf",
                             "时区已持久化到 /etc/opencube.conf"));
        return 1;
    }
    return 0;
}

/* ---- registration ------------------------------------------------------ */

void tui_time_register_pages(void) {
    tui_page_register("builtin-time", "time.clock", "Time: clock & sources",
                      "time", 10, page_clock_render, 0, 0);
    /* order matters: the set page's form consumes Tab (field nav), so
     * the freely-navigable tz page must come BEFORE it - otherwise Tab
     * can never reach the timezone page past the form. */
    tui_page_register("builtin-time", "time.tz", "Time: timezone",
                      "time", 11, page_tz_render, page_tz_input, 0);
    tui_page_register("builtin-time", "time.set", "Time: set the RTC clock",
                      "time", 12, page_set_render, page_set_input, 0);
}

int tui_time_main(void) {
    if (!tui_started()) { if (tui_init() != 0) return -1; }
    return tui_ext_page_main("time.clock");
}
