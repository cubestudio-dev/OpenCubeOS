/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: drivers/misc/driver_gameport.c
 *
 * PC game port driver.  See driver_gameport.h for the contract.  The
 * axis measurement is the classic tick-loop over the one-shot bits:
 * write (fire), then count iterations until each bit drops.  The loop
 * is the time base (no interrupts, no timer hardware needed) - the
 * calibration maps raw ticks into 0..255.
 */
#include "types.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "driver_gameport.h"

static inline u8 inb(u16 p)  { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

static gameport_dev_t *g_gps[GAMEPORT_MAX];
static int g_gp_n;
static int g_inited;

void gameport_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    g_gp_n = 0;
}

int gameport_register(gameport_dev_t *g)
{
    if (!g || !g->io) return -1;
    if (g_gp_n >= GAMEPORT_MAX) return -1;
    for (int i = 0; i < g_gp_n; i++)
        if (g_gps[i] == g || g_gps[i]->io == g->io) return -1;
    if (!g->name[0]) {
        g->name[0] = 'g'; g->name[1] = 'p'; g->name[2] = '0';
        g->name[2] = (char)('0' + g_gp_n);
        g->name[3] = 0;
    }
    for (int a = 0; a < 4; a++) {
        g->cal_min[a] = 0;
        g->cal_max[a] = 0;
        g->cal_center[a] = 0;
    }
    g->calibrated = 0;
    g->present = 1;
    g_gps[g_gp_n++] = g;
    return 0;
}

int gameport_unregister(gameport_dev_t *g)
{
    if (!g) return -1;
    for (int i = 0; i < g_gp_n; i++) {
        if (g_gps[i] == g) {
            for (int k = i; k < g_gp_n - 1; k++)
                g_gps[k] = g_gps[k + 1];
            g_gp_n--;
            g->present = 0;
            return 0;
        }
    }
    return -1;
}

int gameport_count(void) { return g_gp_n; }
gameport_dev_t *gameport_get(int idx)
{
    if (idx < 0 || idx >= g_gp_n) return 0;
    return g_gps[idx];
}

/* ------------------------------------------------------------------ */
/* probing                                                             */
/* ------------------------------------------------------------------ */

/* fire the one-shots and check whether the axis bits actually show a
 * discharge window (a real adapter with or without a stick holds the
 * bits high for a while; an absent port reads static).  We accept the
 * port when firing changes the register state or the buttons carry a
 * plausible pull-up pattern (bits 7..4 high = nothing pressed). */
static int gameport_probe_port(u16 io)
{
    u8 before = inb(io);
    outb(io, 0xFF);                 /* fire */
    u8 after = inb(io);
    u8 after2 = 0;
    for (volatile int d = 0; d < 8; d++) { }
    after2 = inb(io);
    /* buttons pull high on an empty adapter; axis bits move when the
     * one-shots were fired (or are stuck high with no stick - both
     * mean "adapter present").  A dead bus reads the same value for
     * every sample AND never shows button pull-ups. */
    int btn_up = ((before & 0xF0) == 0xF0);
    int moved = (before != after) || (after != after2);
    return (btn_up || moved);
}

void gameport_probe_all(void)
{
    if (!gameport_probe_port(GAMEPORT_IO)) return;
    gameport_dev_t *g = (gameport_dev_t *)kmalloc(sizeof(*g));
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->io = GAMEPORT_IO;
    g->name[0] = 'g'; g->name[1] = 'p'; g->name[2] = '0'; g->name[3] = 0;
    if (gameport_register(g) != 0) kfree(g);
}

/* ------------------------------------------------------------------ */
/* raw measurement                                                     */
/* ------------------------------------------------------------------ */

/* fire + measure all four axes in one pass (they share the one-shot
 * trigger, so one write serves all axes).  timeout_ticks caps the
 * loop for absent sticks.  Returns the raw tick counts, 0xFFFFFFFF
 * for an axis that never dropped. */
#define GP_TIMEOUT_TICKS 200000

static void gameport_fire_and_measure(gameport_dev_t *g, u32 raw[4])
{
    u8 bits;
    u32 t;
    outb(g->io, 0xFF);                    /* fire the one-shots */
    for (int a = 0; a < 4; a++) raw[a] = GAMEPORT_NO_AXIS;
    t = 0;
    for (;;) {
        bits = inb(g->io);                /* bit3..0 = X2 Y2 X1 Y1 */
        int done = 1;
        for (int a = 0; a < 4; a++) {
            u8 m = (u8)(1u << a);
            if (raw[a] == GAMEPORT_NO_AXIS && (bits & m) == 0)
                raw[a] = t;
            if (raw[a] == GAMEPORT_NO_AXIS) done = 0;
        }
        if (done || t >= GP_TIMEOUT_TICKS) break;
        t++;
    }
    if (t >= GP_TIMEOUT_TICKS) g->timeouts++;
    g->reads++;
}

/* ------------------------------------------------------------------ */
/* L1 interfaces                                                       */
/* ------------------------------------------------------------------ */

int gameport_read_axis(gameport_dev_t *g, u8 axis, u32 *out)
{
    if (!g || !g->present || axis > 3 || !out) return -1;
    u32 raw[4];
    gameport_fire_and_measure(g, raw);
    if (raw[axis] == GAMEPORT_NO_AXIS) {
        *out = GAMEPORT_NO_AXIS;
        return 0;                          /* honest "no stick" value */
    }
    if (!g->calibrated) {
        /* uncalibrated: report raw ticks scaled down to 0..255 with a
         * nominal 0..GP_TIMEOUT_TICKS/2 window so the number is still
         * usable before calibration */
        u32 v = raw[axis] * 255u / (GP_TIMEOUT_TICKS / 2);
        *out = v > 255 ? 255 : v;
        return 0;
    }
    u32 mn = g->cal_min[axis], mx = g->cal_max[axis];
    if (mx <= mn) { *out = 128; return 0; }   /* degenerate window */
    u32 span = mx - mn;
    u32 v = (raw[axis] - mn) * 255u / span;
    *out = v > 255 ? 255 : v;
    return 0;
}

int gameport_read_buttons(gameport_dev_t *g, u8 *out)
{
    if (!g || !g->present || !out) return -1;
    u8 v = inb(g->io);
    *out = (u8)((~v >> 4) & 0x0F);         /* active low -> pressed set */
    return 0;
}

int gameport_calibrate(gameport_dev_t *g)
{
    if (!g || !g->present) return -1;
    /* sample a burst of readings per axis and take the extremes;
     * the caller is expected to be moving the stick during this */
    u32 mn[4], mx[4];
    for (int a = 0; a < 4; a++) { mn[a] = GAMEPORT_NO_AXIS; mx[a] = 0; }
    for (int s = 0; s < 32; s++) {
        u32 raw[4];
        gameport_fire_and_measure(g, raw);
        for (int a = 0; a < 4; a++) {
            if (raw[a] == GAMEPORT_NO_AXIS) continue;
            if (raw[a] < mn[a]) mn[a] = raw[a];
            if (raw[a] > mx[a]) mx[a] = raw[a];
        }
    }
    for (int a = 0; a < 4; a++) {
        if (mn[a] == GAMEPORT_NO_AXIS) {
            /* axis never discharged: keep zero window, honest state */
            g->cal_min[a] = 0;
            g->cal_max[a] = 0;
            g->cal_center[a] = 0;
            continue;
        }
        g->cal_min[a] = mn[a];
        g->cal_max[a] = mx[a] > mn[a] ? mx[a] : mn[a] + 1;
        g->cal_center[a] = (g->cal_min[a] + g->cal_max[a]) / 2;
    }
    g->calibrated = 1;
    return 0;
}
