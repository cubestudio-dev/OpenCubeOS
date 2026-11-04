/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocbl_state
 * File: ocbl/common/ocbl_statemon.c
 * Purpose: OCBL boot-state monitor implementation - see
 *          ocbl_statemon.h for the call order and
 *          include/ocbl_state.h for the shared contract.
 *
 *          Everything here is freestanding: plain fixed-address
 *          memory, CMOS RTC port reads, one 8042 pulse, the shared
 *          watchdog driver and the platform console.  Both flavors
 *          (BIOS + UEFI) compile this file unchanged.
 */
#include "ocbl_statemon.h"
#include "ocbl_serial.h"
#include "ocbl_platform.h"
#include "ocbl_wdt.h"
#include "ocbl_mmap.h"
#include "ocbl_state.h"        /* the shared contract (include/) */

/* monitor availability this boot (cleared when the fixed pages are
 * not free for us - then NOTHING here touches low memory and the
 * kernel is told through the " oc.stmon=off" cmdline flag) */
static int g_monitor_ok = 1;

/* ---- raw port I/O -------------------------------------------------- */
static void port_outb(u16 p, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "dN"(p));
}
static u8 port_inb(u16 p)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "dN"(p));
    return v;
}

/* ---- CMOS RTC (mirrors the kernel's core_rtc decode so both ends
 * of the silence measurement share the exact same time scale) ----- */
static u8 rtc_reg(u8 reg)
{
    port_outb(0x70, reg);
    return port_inb(0x71);
}

/* days since 1970-01-01 (Howard Hinnant's civil-days algorithm -
 * identical to the kernel's core_rtc.c) */
static u64 rtc_days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (u64)era * 146097 + doe - 719468;
}

u32 ocbl_rtc_unix_now(void)
{
    u8 second = 0, minute = 0, hour = 0, day = 1, month = 1;
    u8 year = 0, century = 20, regb = 0, pm = 0;

    /* consistent snapshot (re-read on a race, like the kernel's
     * core_rtc_read; both reads bound the loop) */
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!(rtc_reg(0x0A) & 0x80)) {
            second = rtc_reg(0x00);
            minute = rtc_reg(0x02);
            hour = rtc_reg(0x04);
            day = rtc_reg(0x07);
            month = rtc_reg(0x08);
            year = rtc_reg(0x09);
            century = rtc_reg(0x48);
            regb = rtc_reg(0x0B);
            if (!(rtc_reg(0x0A) & 0x80) && second == rtc_reg(0x00))
                break;
        }
    }
    /* regB conventions (MC146818): bit1=0 -> 12h mode (bit7 of the
     * hour register is the PM flag), bit2=0 -> BCD digits.  Decode
     * exactly like the kernel (the PM flag is stripped BEFORE the
     * BCD conversion). */
    if (!(regb & 0x02)) {
        pm = hour & 0x80;
        hour &= 0x7F;
    }
    if (!(regb & 0x04)) {
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
        if (hour == 12) hour = 0;
        if (pm) hour = (u8)(hour + 12);
    }
    {
        int full_year = (century >= 19 && century <= 21)
                            ? century * 100 + year : 2000 + year;
        if (month < 1) month = 1;
        if (month > 12) month = 12;
        if (day < 1) day = 1;
        u64 days = rtc_days_from_civil(full_year, month, day);
        return (u32)(days * 86400ULL + (u64)hour * 3600 +
                     (u64)minute * 60 + (u64)second);
    }
}

/* busy-wait `sec` seconds on the RTC seconds field (OCBL has no
 * timer interrupts; the RTC is the one always-present clock) */
void ocbl_rtc_sleep_sec(int sec)
{
    u8 last = rtc_reg(0x00);
    int seen = 0;
    while (seen < sec) {
        u8 now = rtc_reg(0x00);
        if (now != last) {
            last = now;
            seen++;
        }
    }
}

/* ---- the 8042 reset pulse ------------------------------------------ */
static void pulse_8042_reset(void)
{
    /* wait for the 8042 input buffer to drain (bounded), then pulse
     * the reset line: keyboard-controller command 0xFE */
    for (int i = 0; i < 100000; i++) {
        if (!(port_inb(0x64) & 0x02))
            break;
    }
    port_outb(0x64, 0xFE);
    /* the pulse resets the machine within microseconds on real
     * hardware and instantly in QEMU; nothing below should run */
    for (;;)
        __asm__ volatile("hlt");
}

/* ---- the screen notice --------------------------------------------- */
static void show_failure_notice(const char* stage_name)
{
    ocbl_plat_con_init();
    ocbl_plat_con_line(8, 1, "Open Cube OS failed to boot at stage:",
                       OCBL_CON_BRIGHT);
    ocbl_plat_con_line(10, 1, stage_name, OCBL_CON_BRIGHT);
    ocbl_plat_con_line(12, 1, "Rebooting to recovery...",
                       OCBL_CON_PLAIN);
}

/* ---- the state area + record accessors ------------------------------ */
/* volatile throughout: these are cross-boot mailbox blocks (read and
 * written across resets) - the compiler must never cache or reorder
 * accesses (live-caught: gcc -O2 reordered the routed-flip around the
 * checksum recompute and OCIRE then read a record whose checksum
 * matched the PRE-flip bytes) */
static volatile ocst_state_t* st(void)
    { return (volatile ocst_state_t*)OCST_STATE_ADDR; }
static volatile ocst_record_t* rec(void)
    { return (volatile ocst_record_t*)OCST_RESERVED_ADDR; }

static void state_init(u16 seq, u32 now)
{
    volatile ocst_state_t* s = st();
    s->magic = OCST_STATE_MAGIC;
    s->ver = OCST_STATE_VER;
    s->size = sizeof(ocst_state_t);
    s->seq = seq;
    s->stage = OCST_STAGE_RESET;
    s->armed = 0;
    s->kflags = 0;
    s->rsvd0 = 0;
    s->heartbeat = 0;
    s->last_progress = now;
}

static void record_write(u8 kind, u8 stall, u16 seq, u32 ts,
                         u32 silence, u8 routed)
{
    volatile ocst_record_t* r = rec();
    r->magic = OCST_RESERVED_MAGIC;
    r->ver = OCST_RESERVED_VER;
    r->size = sizeof(ocst_record_t);
    r->kind = kind;
    r->stall = stall;
    r->seq = seq;
    r->ts_unix = ts;
    r->silence_sec = silence;
    r->routed = routed;
    r->rsvd0 = 0;
    r->rsvd1 = 0;
    r->checksum = ocst_record_checksum(r);
}

/* ---- the BIOS-flavor window check ----------------------------------- */
int ocbl_statemon_bios_window_ok(void)
{
    const ocbl_mmap_t* mm = ocbl_mmap_get();
    const u64 lo = (u64)OCST_STATE_ADDR;
    const u64 hi = (u64)OCST_RESERVED_ADDR + OCST_RESERVED_SIZE;
    for (u32 i = 0; i < mm->count; i++) {
        if (mm->entries[i].type != 1 || mm->entries[i].len == 0)
            continue;
        if (mm->entries[i].addr <= lo &&
            mm->entries[i].addr + mm->entries[i].len >= hi)
            return 1;
    }
    return 0;
}

/* ---- 1. the early boot diagnosis ------------------------------------ */
void ocbl_statemon_boot_check(int window_ok)
{
    volatile ocst_state_t* s = st();
    u32 now = ocbl_rtc_unix_now();

    if (!window_ok) {
        g_monitor_ok = 0;
        ocbl_serial_puts("[state] monitor window unavailable - state "
                         "monitoring DISABLED this boot\r\n");
        return;
    }
    g_monitor_ok = 1;

    if (ocst_state_valid(s)) {
        int clean = (s->kflags & OCST_KFLAG_CLEAN) != 0;
        int judged = (s->kflags & OCST_KFLAG_JUDGED) != 0;
        int progress = (s->armed || s->stage > OCST_STAGE_RESET);
        u32 silence = now - s->last_progress;

        if (!clean && !judged && progress &&
            silence >= OCST_SILENCE_FAIL_SEC && silence <= 3600u) {
            /* JUDGED FAILURE: the previous payload was mid-boot with
             * the relay armed and nothing has touched the state area
             * for the measured silence - the watchdog (or the hang
             * it caught) reset the machine here. */
            u8 stall = s->stage;
            record_write(OCST_KIND_BOOT_FAILED, stall, s->seq, now,
                         silence, 0);
            ocbl_serial_puts("[state] previous boot judged FAILED at stage ");
            ocbl_serial_puts(ocst_stage_name(stall));
            ocbl_serial_puts(" (silence ");
            ocbl_serial_dec(silence);
            ocbl_serial_puts("s, seq ");
            ocbl_serial_dec(s->seq);
            ocbl_serial_puts(") - BOOT_FAILED recorded\r\n");
            ocbl_serial_puts("Open Cube OS failed to boot at stage: ");
            ocbl_serial_puts(ocst_stage_name(stall));
            ocbl_serial_puts("\r\nRebooting to recovery...\r\n");
            show_failure_notice(ocst_stage_name(stall));
            /* mark the stall JUDGED in the state area BEFORE the
             * pulse, so the boot after the reset routes into OCIRE
             * instead of re-judging the same silence again */
            s->kflags = OCST_KFLAG_JUDGED;
            ocbl_rtc_sleep_sec((int)OCST_FAIL_DWELL_SEC);
            pulse_8042_reset();               /* never returns */
        }

        if (clean) {
            ocbl_serial_puts("[state] previous boot shut down cleanly (seq ");
            ocbl_serial_dec(s->seq);
            ocbl_serial_puts(")\r\n");
        } else if (judged) {
            ocbl_serial_puts("[state] previous stall already judged - "
                             "routing to recovery\r\n");
        } else if (progress && silence > 3600u) {
            ocbl_serial_puts("[state] rtc jump detected (silence ");
            ocbl_serial_dec(silence);
            ocbl_serial_puts("s) - not judging a failure\r\n");
        } else if (progress) {
            ocbl_serial_puts("[state] warm reset with recent progress (silence ");
            ocbl_serial_dec(silence);
            ocbl_serial_puts("s) - not a failure\r\n");
        } else {
            ocbl_serial_puts("[state] previous boot armed no payload - no failure\r\n");
        }
        state_init((u16)(s->seq + 1), now);
    } else {
        ocbl_serial_puts("[state] fresh boot - state area initialized\r\n");
        state_init(1, now);
    }
    ocbl_serial_puts("[state] boot attempt seq ");
    ocbl_serial_dec(st()->seq);
    ocbl_serial_puts("\r\n");
}

/* ---- 2. the pending-failure auto route ------------------------------- */
int ocbl_statemon_route_pending(void)
{
    volatile ocst_record_t* r = rec();
    if (!g_monitor_ok)
        return 0;
    if (!ocst_record_valid(r) || r->kind != OCST_KIND_BOOT_FAILED ||
        r->routed)
        return 0;
    r->routed = 1;
    r->checksum = ocst_record_checksum(r);   /* volatile: see header */
    ocbl_serial_puts("[state] BOOT_FAILED record pending (stall ");
    ocbl_serial_puts(ocst_stage_name(r->stall));
    ocbl_serial_puts(") - auto-booting OCIRE\r\n");
    return 1;
}

/* ---- 3. the user-request record -------------------------------------- */
void ocbl_statemon_user_request(void)
{
    u32 now = ocbl_rtc_unix_now();
    if (!g_monitor_ok)
        return;
    record_write(OCST_KIND_USER_REQUEST, 0, st()->seq, now, 0, 1);
    ocbl_serial_puts("[state] USER_REQUEST recorded - recovery session\r\n");
}

/* ---- 4. the /HANGAT diagnostic hook ---------------------------------- */
static u8 g_hangat;                     /* 0 = absent */

void ocbl_statemon_probe_hangat(const ocbl_volume_t* v)
{
    char c = 0;
    g_hangat = 0;
    if (!v || !g_monitor_ok)
        return;
    if (ocbl_fs_read_file(v, "/HANGAT", &c, 1) == 1 &&
        c >= '1' && c <= '7') {
        g_hangat = (u8)c;
        ocbl_serial_puts("[state] /HANGAT diagnostic hook: oc.hang=");
        ocbl_serial_putc(c);
        ocbl_serial_puts(" (the kernel will halt after the stage)\r\n");
    }
}

const char* ocbl_statemon_cmdline_extra(void)
{
    static char buf[32];
    u32 o = 0;
    if (!g_monitor_ok) {
        static const char off[] = " oc.stmon=off";
        for (u32 k = 0; off[k]; k++)
            buf[o++] = off[k];
        buf[o] = 0;
        return buf;
    }
    if (!g_hangat)
        return "";
    buf[o++] = ' ';
    buf[o++] = 'o';
    buf[o++] = 'c';
    buf[o++] = '.';
    buf[o++] = 'h';
    buf[o++] = 'a';
    buf[o++] = 'n';
    buf[o++] = 'g';
    buf[o++] = '=';
    buf[o++] = (char)g_hangat;
    buf[o] = 0;
    return buf;
}

/* ---- 5. arm the monitored payload handoff ----------------------------- */
void ocbl_statemon_arm_payload(void)
{
    volatile ocst_state_t* s = st();
    if (!g_monitor_ok) {
        ocbl_serial_puts("[state] monitor disabled - payload not armed\r\n");
        return;
    }
    s->armed = 1;
    s->stage = OCST_STAGE_RESET;
    s->kflags = 0;
    s->heartbeat = 0;
    s->last_progress = ocbl_rtc_unix_now();
    if (ocbl_wdt_present()) {
        ocbl_wdt_arm((int)ocst_stage_budget(OCST_STAGE_RESET));
        ocbl_serial_puts("[state] payload armed: boot budget ");
        ocbl_serial_dec(ocst_stage_budget(OCST_STAGE_RESET));
        ocbl_serial_puts("s on the watchdog relay\r\n");
    } else {
        ocbl_serial_puts("[state] payload armed: NO 6300ESB watchdog - "
                         "detection degrades to the next reset\r\n");
    }
}
