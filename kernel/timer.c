/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/timer.c
 * Purpose: PIT driver + system tick + soft timers.
 */
#include "timer.h"
#include "idt.h"
#include "pic.h"
#include "irq.h"
#include "string.h"

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* 8253/8254 PIT registers. */
#define PIT_CH0_DATA  0x40
#define PIT_CMD       0x43
#define PIT_HZ        1193182u  /* PIT base frequency */

static u64 g_tick_count = 0;
static u64 g_boot_ms    = 0;

/* Soft timer table. */
#define OC_MAX_TIMERS 16
typedef struct {
    int             in_use;
    int             periodic;
    u64             next_fire_ms;  /* absolute time of next fire */
    u64             interval_ms;   /* for periodic: repeat interval */
    oc_timer_cb_fn  fn;
    void           *ctx;
} oc_soft_timer_t;

static oc_soft_timer_t g_timers[OC_MAX_TIMERS];

void oc_timer_init(void) {
    oc_memset(g_timers, 0, sizeof(g_timers));
    g_tick_count = 0;
    g_boot_ms    = 0;

    /* Program PIT channel 0: mode 2 (rate generator), 16-bit reload.
     * divisor = PIT_HZ / OC_TIMER_HZ = 11932 (for 100 Hz). */
    u32 divisor = PIT_HZ / OC_TIMER_HZ;
    outb(PIT_CMD, 0x34);  /* channel 0, lobyte/hibyte, mode 2, binary */
    outb(PIT_CH0_DATA, (u8)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (u8)((divisor >> 8) & 0xFF));

    /* Register the IRQ0 handler. */
    oc_irq_register_handler(0, oc_timer_irq_handler, NULL);
}

u64 oc_timer_now_ms(void) {
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

u64 oc_timer_ticks(void) {
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

void oc_timer_format_hms(u64 ms, char out[16]) {
    u64 total = ms;
    u32 h  = (u32)(total / 3600000); total %= 3600000;
    u32 m  = (u32)(total / 60000);   total %= 60000;
    u32 s  = (u32)(total / 1000);
    u32 ms3 = (u32)(total % 1000);
    /* Build "HH:MM:SS.mmm" zero-padded. */
    char num[8];
    oc_u64_to_str(h, num);  if (oc_strlen(num) < 2) { num[2]=0; num[1]=num[0]; num[0]='0'; }
    oc_strcpy(out, num); out[2]=':';
    oc_u64_to_str(m, num);  if (oc_strlen(num) < 2) { num[2]=0; num[1]=num[0]; num[0]='0'; }
    oc_strcpy(out+3, num); out[5]=':';
    oc_u64_to_str(s, num);  if (oc_strlen(num) < 2) { num[2]=0; num[1]=num[0]; num[0]='0'; }
    oc_strcpy(out+6, num); out[8]='.';
    /* BUG-037 FIX: Zero-pad milliseconds to 3 digits (.mmm).
     * Old code printed ms3 without padding → .80 instead of .080. */
    oc_u64_to_str(ms3, num);
    if (oc_strlen(num) == 1) { num[3]=0; num[2]=num[0]; num[1]='0'; num[0]='0'; }
    else if (oc_strlen(num) == 2) { num[3]=0; num[2]=num[1]; num[1]=num[0]; num[0]='0'; }
    oc_strcpy(out+9, num); out[12]=0;
}

int oc_timer_register_periodic(oc_timer_cb_fn fn, void *ctx, u64 interval_ms) {
    if (!fn || interval_ms == 0) return -1;
    for (int i = 0; i < OC_MAX_TIMERS; i++) {
        if (!g_timers[i].in_use) {
            g_timers[i].in_use        = 1;
            g_timers[i].periodic      = 1;
            g_timers[i].interval_ms   = interval_ms;
            g_timers[i].next_fire_ms  = oc_timer_now_ms() + interval_ms;
            g_timers[i].fn            = fn;
            g_timers[i].ctx           = ctx;
            return i;
        }
    }
    return -2;
}

int oc_timer_register_oneshot(oc_timer_cb_fn fn, void *ctx, u64 delay_ms) {
    if (!fn || delay_ms == 0) return -1;
    for (int i = 0; i < OC_MAX_TIMERS; i++) {
        if (!g_timers[i].in_use) {
            g_timers[i].in_use        = 1;
            g_timers[i].periodic      = 0;
            g_timers[i].interval_ms   = delay_ms;
            g_timers[i].next_fire_ms  = oc_timer_now_ms() + delay_ms;
            g_timers[i].fn            = fn;
            g_timers[i].ctx           = ctx;
            return i;
        }
    }
    return -2;
}

int oc_timer_cancel(int id) {
    if (id < 0 || id >= OC_MAX_TIMERS) return -1;
    if (!g_timers[id].in_use) return -2;
    g_timers[id].in_use = 0;
    return 0;
}

/* The IRQ0 handler. Called from oc_irq_dispatch. */
void oc_timer_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)ctx; (void)f;
    g_tick_count++;
    g_boot_ms += OC_TIMER_MS_PER_TICK;

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
    extern void sched_tick(void);
    sched_tick();
}
