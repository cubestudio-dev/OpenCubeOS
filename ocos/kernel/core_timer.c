/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02 / WP-compat-and-security-hardening B4
 * File: kernel/core/core_timer.c
 * Purpose: system tick + soft timers - RTC-first, PIT fallback.
 *
 * WP-compat-and-security-hardening B4: the system heartbeat is the
 * CMOS RTC periodic interrupt (PIE @ 1024 Hz, IRQ8, fanned out by the
 * single RegC-owning handler in drv_time_cmos_rtc.c) whenever the
 * RTC hardware answers; the 8254 PIT path below is the FALLBACK and
 * runs only when the RTC is absent/broken (or oc.tick=pit forces it
 * for the acceptance test).  The 1024 Hz tick is converted to whole
 * milliseconds with a half-nanosecond accumulator - exact by
 * construction, zero cumulative drift. */
#include "core_timer.h"
#include "arch_idt.h"
#include "arch_pic.h"
#include "arch_irq.h"
#include "core_lib_string.h"
#include "core_lib_klog.h"
#include "drv_time_cmos_rtc.h"

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* 8253/8254 PIT registers. */
#define PIT_CH0_DATA  0x40
#define PIT_CMD       0x43
#define PIT_HZ        1193182u  /* PIT base frequency */

static u64 g_tick_count = 0;
static u64 g_boot_ms    = 0;

/* ---- WP-compat-and-security-hardening B4: the tick source ---------- */
static int  g_tick_src = 0;   /* 0 = PIT (legacy), 1 = RTC 1024 Hz */
static u32  g_rtc_sub;        /* half-ns accumulator: 1024 Hz period
                               * = 976 562.5 ns = 1 953 125 half-ns */
#define RTC_HALF_NS_PER_TICK 1953125u
#define HALF_NS_PER_MS       2000000u

/* Soft timer table. */
#define OC_MAX_TIMERS 16
typedef struct {
    int             in_use;
    int             periodic;
    u64             next_fire_ms;  /* absolute time of next fire */
    u64             interval_ms;   /* for periodic: repeat interval */
    core_timer_cb_fn  fn;
    void           *ctx;
} core_timer_soft_timer_t;

static core_timer_soft_timer_t g_timers[OC_MAX_TIMERS];

void core_timer_init(const char *cmdline) {
    memset(g_timers, 0, sizeof(g_timers));
    g_tick_count = 0;
    g_boot_ms    = 0;
    g_rtc_sub    = 0;

    /* WP-compat-and-security-hardening B4: oc.tick= override for the
     * acceptance path ("rtc" / "pit"; anything else = auto).  Auto is
     * the default and the RTC WINS whenever the hardware answers -
     * the PIT is the fallback, not the peer. */
    int want = 0;   /* 0 auto, 1 forced rtc, 2 forced pit */
    if (cmdline) {
        const char *p = cmdline;
        while (*p) {
            if ((p == cmdline || p[-1] == ' ') &&
                strncmp(p, "oc.tick=", 8) == 0) {
                if      (p[8] == 'r') want = 1;   /* rtc */
                else if (p[8] == 'p') want = 2;   /* pit */
                break;
            }
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
    }

    if (want != 2) {
        if (driver_cmos_rtc_init() == 0) {
            if (driver_cmos_rtc_pie_arm_system(6, core_timer_rtc_tick) == 0) {
                g_tick_src = 1;
                klog_record(KLOG_INFO, "timer",
                            "RTC 1024 Hz system tick armed (PIT idle)");
                return;
            }
        }
        if (want == 1)
            klog_write(KLOG_WARN, "timer",
                       "oc.tick=rtc forced but RTC unavailable - PIT fallback");
    }

    /* ---- PIT fallback (the WP-02 path, byte-for-byte) ---------------- */
    g_tick_src = 0;

    /* Program PIT channel 0: mode 2 (rate generator), 16-bit reload.
     * divisor = PIT_HZ / OC_TIMER_HZ = 11932 (for 100 Hz). */
    u32 divisor = PIT_HZ / OC_TIMER_HZ;
    outb(PIT_CMD, 0x34);  /* channel 0, lobyte/hibyte, mode 2, binary */
    outb(PIT_CH0_DATA, (u8)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (u8)((divisor >> 8) & 0xFF));

    /* Register the IRQ0 handler. */
    arch_irq_register_handler(0, core_timer_irq_handler, NULL);
    klog_record(KLOG_INFO, "timer", "PIT 100 Hz fallback tick armed");
}

void core_timer_resume(void) {
    /* fix2: reprogram the tick source only. The soft-timer table and
     * the tick counters live in RAM and survived S3; wiping them here
     * (the old core_timer_init() call) silently killed the network
     * poll timer on every resume. The IRQ handlers stay registered -
     * the dispatch table is RAM-resident too.
     * B4: re-arm whichever source owns the tick - the S3 resume path
     * may have left the RTC's PIE regime cleared by firmware. */
    if (g_tick_src == 1) {
        (void)driver_cmos_rtc_pie_arm_system(6, core_timer_rtc_tick);
        return;
    }
    u32 divisor = PIT_HZ / OC_TIMER_HZ;
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0_DATA, (u8)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (u8)((divisor >> 8) & 0xFF));
}

const char *core_timer_source(void) {
    return (g_tick_src == 1) ? "rtc" : "pit";
}

u64 core_timer_now_ms(void) {
    u64 ms;
    u64 flags;
    /* Save rflags, disable interrupts, read the 64-bit counter atomically,
     * then restore the original interrupt state. This is safe to call
     * both before and after IDT/timer init. */
    __asm__ volatile(
        "pushfq\n"
        "popq %1\n"        /* save flags */
        "cli\n"            /* disable interrupts */
        "movq %2, %0\n"    /* read g_boot_ms */
        "pushq %1\n"
        "popfq\n"          /* restore flags (and IF) */
        : "=r"(ms), "=&r"(flags)
        : "m"(g_boot_ms)
        : "memory"
    );
    return ms;
}

u64 core_timer_ticks(void) {
    u64 t;
    u64 flags;
    __asm__ volatile(
        "pushfq\n"
        "popq %1\n"
        "cli\n"
        "movq %2, %0\n"
        "pushq %1\n"
        "popfq\n"
        : "=r"(t), "=&r"(flags)
        : "m"(g_tick_count)
        : "memory"
    );
    return t;
}

void core_timer_format_hms(u64 ms, char out[16]) {
    u64 total = ms;
    u32 h  = (u32)(total / 3600000); total %= 3600000;
    u32 m  = (u32)(total / 60000);   total %= 60000;
    u32 s  = (u32)(total / 1000);
    u32 ms3 = (u32)(total % 1000);
    /* Build "HH:MM:SS.mmm" zero-padded. */
    char num[8];
    u64_to_str(h, num);  if (strlen(num) < 2) { num[2]=0; num[1]=num[0]; num[0]='0'; }
    strcpy(out, num); out[2]=':';
    u64_to_str(m, num);  if (strlen(num) < 2) { num[2]=0; num[1]=num[0]; num[0]='0'; }
    strcpy(out+3, num); out[5]=':';
    u64_to_str(s, num);  if (strlen(num) < 2) { num[2]=0; num[1]=num[0]; num[0]='0'; }
    strcpy(out+6, num); out[8]='.';
    /* BUG-037 FIX: Zero-pad milliseconds to 3 digits (.mmm).
     * Old code printed ms3 without padding → .80 instead of .080. */
    u64_to_str(ms3, num);
    if (strlen(num) == 1) { num[3]=0; num[2]=num[0]; num[1]='0'; num[0]='0'; }
    else if (strlen(num) == 2) { num[3]=0; num[2]=num[1]; num[1]=num[0]; num[0]='0'; }
    strcpy(out+9, num); out[12]=0;
}

/* BUG-0289 FIX (periodic twin, hygiene): register_periodic had the same
 * one-shot registration race BUG-0289 fixed for register_oneshot. It
 * published in_use FIRST and filled periodic/next_fire_ms/fn afterwards
 * with interrupts enabled, so an IRQ0 scan hitting that window saw a
 * timer that was "in use" with a stale (or zero, i.e. due-now)
 * next_fire_ms and a stale/NULL fn: a fresh BSS slot was released by
 * the scan (in_use = 0 — the registration then completed into a
 * cancelled slot and returned a valid id for a timer that never
 * fires), and a reused slot could fire the PREVIOUS occupant's
 * callback. Same two cheap defences as the one-shot fix: interrupts
 * stay disabled for the few instructions of the fill (the IRQ0 scan
 * cannot interleave at all), and in_use is published LAST so even a
 * hypothetical observer never sees a half-filled slot. Flags are
 * restored with popfq (not sti) so a caller that is already inside a
 * cli section keeps IF=0. */
int core_timer_register_periodic(core_timer_cb_fn fn, void *ctx, u64 interval_ms) {
    if (!fn || interval_ms == 0) return -1;
    u64 flags;
    __asm__ volatile(
        "pushfq\n"
        "popq %0\n"
        "cli\n"
        : "=r"(flags)
        :
        : "memory");
    for (int i = 0; i < OC_MAX_TIMERS; i++) {
        if (!g_timers[i].in_use) {
            g_timers[i].periodic      = 1;
            g_timers[i].interval_ms   = interval_ms;
            /* core_timer_now_ms is itself irqsave (pushf/cli/popf), safe
             * to nest inside this window; g_boot_ms cannot advance while
             * IF=0 so the first deadline is exact. */
            g_timers[i].next_fire_ms  = core_timer_now_ms() + interval_ms;
            g_timers[i].fn            = fn;
            g_timers[i].ctx           = ctx;
            __asm__ volatile("mfence" ::: "memory");
            g_timers[i].in_use        = 1;   /* publish LAST */
            __asm__ volatile(
                "pushq %0\n"
                "popfq\n"
                :
                : "r"(flags)
                : "memory");
            return i;
        }
    }
    __asm__ volatile(
        "pushq %0\n"
        "popfq\n"
        :
        : "r"(flags)
        : "memory");
    return -2;
}

/* BUG-0289 FIX (A3-12): the whole find+fill sequence runs inside an
 * irqsave window.  The old code published in_use FIRST and filled
 * next_fire_ms/fn afterwards with interrupts enabled, so an IRQ0 scan
 * hitting that window saw a timer that was "in use, due now, with a
 * NULL fn" and released the slot (in_use = 0) - the registration then
 * completed into a cancelled slot and returned a valid id for a timer
 * that would never fire (or, on slot reuse, fire a stale callback).
 * Two defences, both cheap: interrupts stay disabled for the few
 * instructions of the fill (so the IRQ0 scan cannot interleave at
 * all), and in_use is published LAST so even a hypothetical observer
 * never sees a half-filled slot.  Flags are restored with popfq (not
 * sti) so a caller that is already inside a cli section keeps IF=0,
 * same convention as the spinlocks. */
int core_timer_register_oneshot(core_timer_cb_fn fn, void *ctx, u64 delay_ms) {
    if (!fn || delay_ms == 0) return -1;
    u64 flags;
    __asm__ volatile(
        "pushfq\n"
        "popq %0\n"
        "cli\n"
        : "=r"(flags)
        :
        : "memory");
    for (int i = 0; i < OC_MAX_TIMERS; i++) {
        if (!g_timers[i].in_use) {
            g_timers[i].periodic      = 0;
            g_timers[i].interval_ms   = delay_ms;
            /* core_timer_now_ms is itself irqsave (pushf/cli/popf), safe
             * to nest inside this window; g_boot_ms cannot advance while
             * IF=0 so the deadline is exact. */
            g_timers[i].next_fire_ms  = core_timer_now_ms() + delay_ms;
            g_timers[i].fn            = fn;
            g_timers[i].ctx           = ctx;
            __asm__ volatile("mfence" ::: "memory");
            g_timers[i].in_use        = 1;   /* publish LAST */
            __asm__ volatile(
                "pushq %0\n"
                "popfq\n"
                :
                : "r"(flags)
                : "memory");
            return i;
        }
    }
    __asm__ volatile(
        "pushq %0\n"
        "popfq\n"
        :
        : "r"(flags)
        : "memory");
    return -2;
}

int core_timer_cancel(int id) {
    if (id < 0 || id >= OC_MAX_TIMERS) return -1;
    if (!g_timers[id].in_use) return -2;
    g_timers[id].in_use = 0;
    return 0;
}

/* The soft-timer + scheduler body, shared by both tick sources. */
static void core_timer_soft_and_sched(void) {
    /* Run any due soft timers. */
    for (int i = 0; i < OC_MAX_TIMERS; i++) {
        if (!g_timers[i].in_use) continue;
        if (g_boot_ms >= g_timers[i].next_fire_ms) {
            if (g_timers[i].fn) g_timers[i].fn(g_timers[i].ctx);
            if (g_timers[i].periodic) {
                /* P1-24 FIX: limit catch-up to prevent timer storm. If the
                 * timer fell behind by more than 3 intervals, reset to now
                 * instead of firing all missed intervals. */
                u64 now = g_boot_ms;
                u64 next = g_timers[i].next_fire_ms + g_timers[i].interval_ms;
                if (next < now - g_timers[i].interval_ms * 3) {
                    next = now + g_timers[i].interval_ms;  /* reset to now */
                }
                g_timers[i].next_fire_ms = next;
            } else {
                g_timers[i].in_use = 0;
            }
        }
    }

    /* WP-04: Drive the preemptive scheduler. */
    extern void core_sched_tick(void);
    core_sched_tick();
}

/* ---- WP-compat-and-security-hardening B4: the RTC tick consumer ------
 * Called from the single RegC-owning IRQ8 handler (drv_time_cmos_rtc.c)
 * with the already-drained RegC value.  IRQ context: no waiting, no
 * unregistering - just the accounting and the shared body. */
void core_timer_rtc_tick(u8 regc) {
    if (!(regc & 0x40)) return;         /* only the periodic source */
    g_tick_count++;
    /* 1024 Hz -> whole ms with the half-ns accumulator: each tick is
     * exactly 1 953 125 half-ns; a whole ms is 2 000 000.  The
     * remainder never accumulates error (pure rational arithmetic). */
    g_rtc_sub += RTC_HALF_NS_PER_TICK;
    while (g_rtc_sub >= HALF_NS_PER_MS) {
        g_rtc_sub -= HALF_NS_PER_MS;
        g_boot_ms++;
    }
    core_timer_soft_and_sched();
}

/* The IRQ0 handler. Called from arch_irq_dispatch. */
void core_timer_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    g_tick_count++;
    g_boot_ms += OC_TIMER_MS_PER_TICK;
    core_timer_soft_and_sched();
}
