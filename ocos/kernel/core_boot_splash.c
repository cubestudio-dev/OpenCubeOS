/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-compat-and-security-hardening B5
 * File: kernel/core_boot_splash.c
 * Purpose: the boot splash - a centered ASCII logo plus a phase-driven
 *          progress bar - with the full boot log captured to the kernel
 *          log instead of scrolling past.
 *
 * While the splash is active the screen-console hook intercepts EVERY
 * character the kernel would print: the bytes are aggregated into lines
 * and land in klog (the background boot log, mirrored to
 * /var/log/kernel.log by klog_on_mount + the serial transcript), and
 * the screen itself shows only the logo + the progress bar.  A panic
 * tears the splash down (boot_splash_abort from the crash service's
 * panic entry) so the failure report lands on a clean full-screen
 * console - the classic "splash hides the diagnostics exactly when you
 * need them" trap, closed by construction.
 *
 * Phases map 1:1 onto the boot-state machine (OCST_STAGE_*, see
 * include/ocbl_state.h): 1 kmain entered, 2 sched ready, 3 drivers
 * ready, 4 services ready, 5 L1 launched, 6 L1 ready.  The bar reaches
 * 100% at the L1_LAUNCHED stage; the L1_READY one-shot (ush's FIRST
 * syscall - before its prompt byte is drawn) clears the screen and
 * releases the hook, so the very first thing on the clean screen is
 * the ush> prompt.
 *
 * oc.splash=0 on the kernel command line boots the classic way (full
 * scrolling log, no splash) - a diagnostics switch, not a feature
 * gate: the default experience is the splash. */
#include "core_boot_splash.h"
#include "core_console_screen.h"
#include "core_console_fb.h"     /* screen_fb_rgb */
#include "core_lib_string.h"
#include "core_lib_klog.h"
#include "core_lib_log.h"

/* ---- the logo (7 lines, 56 columns max) ---------------------------- */
static const char *const g_logo[] = {
    "  ___                    _        ____                  ",
    " / _ \\ _ __   ___ _ __ (_)_ __  / ___|  ___ __ _ _ __  ",
    "| | | | '_ \\ / _ \\ '_ \\| | '_ \\| |     / __/ _` | '_ \\ ",
    "| |_| | |_) |  __/ | | | | | | | |___ | (_| (_| | | | |",
    " \\___/| .__/ \\___|_| |_|_|_| |_|\\____| \\___\\__,_|_| |_|",
    "      |_|                                              ",
    0
};

/* phase captions (index = OCST stage, 0 unused) */
static const char *const g_phase_name[] = {
    "starting",
    "kernel entered",
    "scheduler ready",
    "drivers ready",
    "services ready",
    "L1 launched",
    "L1 ready",
};

static int  g_splash_on;
static int  g_splash_stage;      /* 0..6 (the OCST stage) */

/* The hook chain we displaced (kcap's capture hook sits on the console
 * hook with the serial tee as its base - screen_console_get_hook/
 * set_hook is a STACK, not a slot: finish() must RESTORE what begin()
 * displaced, never clear it.  Clearing killed the serial transcript
 * for the whole session - live-caught by the B5 acceptance run.) */
static screen_console_hook_t g_hook_saved;

/* line capture buffer: hook bytes -> lines -> klog */
#define SPLASH_LINE 240
static char g_line[SPLASH_LINE];
static int  g_line_len;


/* ---- rendering ------------------------------------------------------- */

static void splash_putat(int x, int y, const char *s, u32 fg) {
    for (int i = 0; s[i]; i++)
        screen_console_putc_at((u32)(x + i), (u32)y, s[i], fg,
                                screen_fb_rgb(0x00, 0x00, 0x00));
}

static void splash_draw_bar(int stage) {
    u32 cols, rows;
    screen_console_get_size(&cols, &rows);
    if (cols == 0 || rows == 0) return;

    int logo_lines = 0;
    while (g_logo[logo_lines]) logo_lines++;
    int logo_cols = (int)strlen(g_logo[0]);

    /* the splash block: logo + blank + bar + caption (4 lines) */
    int block = logo_lines + 4;
    int top = ((int)rows - block) / 2;
    if (top < 0) top = 0;

    u32 white = screen_fb_rgb(0xE0, 0xE0, 0xE0);
    u32 green = screen_fb_rgb(0x40, 0xC0, 0x70);

    /* logo, centered */
    int xoff = ((int)cols - logo_cols) / 2;
    if (xoff < 0) xoff = 0;
    for (int i = 0; g_logo[i]; i++)
        splash_putat(xoff, top + i, g_logo[i], white);

    /* blank line, then the progress bar: [=======>    ] NN% PHASE m/6 */
    int pct = stage >= 6 ? 100 : (stage * 100) / 6;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    char bar[80];
    int width = 40;               /* fixed bar width, centered like the logo */
    int fill = (width * pct) / 100;
    int o = 0;
    bar[o++] = '[';
    for (int i = 0; i < width; i++) {
        if (i < fill - 1)      bar[o++] = '=';
        else if (i == fill - 1) bar[o++] = '>';
        else                   bar[o++] = ' ';
    }
    bar[o++] = ']';
    bar[o++] = ' ';
    /* NN% */
    if (pct == 100) { bar[o++] = '1'; bar[o++] = '0'; bar[o++] = '0'; }
    else { bar[o++] = (char)('0' + (pct / 10)); bar[o++] = (char)('0' + (pct % 10)); }
    bar[o++] = '%';
    bar[o] = 0;

    int bar_y = top + logo_lines + 1;
    int bx = ((int)cols - (int)strlen(bar)) / 2;
    if (bx < 0) bx = 0;
    splash_putat(bx, bar_y, bar, green);

    /* caption: PHASE m/6 <name> */
    const char *cap = g_phase_name[(stage >= 0 && stage <= 6) ? stage : 0];
    char line[80];
    strcpy(line, "PHASE ");
    line[6] = (char)('0' + (stage > 6 ? 6 : stage));
    line[7] = '/'; line[8] = '6'; line[9] = ' '; line[10] = 0;
    strcat(line, cap);
    int cx = ((int)cols - (int)strlen(line)) / 2;
    if (cx < 0) cx = 0;
    splash_putat(cx, bar_y + 1, line, white);

    screen_console_move_cursor(0, rows > 0 ? rows - 1 : 0);
}

/* ---- the capture hook ------------------------------------------------- */

static int splash_hook(void *ctx, u8 ch) {
    (void)ctx;
    /* Aggregate into lines; drop control noise (CR/BS/CSI bytes) so
     * the background log stays plain text. */
    if (ch == '\n') {
        g_line[g_line_len] = 0;
        if (g_line_len > 0)
            klog_record(KLOG_INFO, "boot", g_line);
        g_line_len = 0;
        return 1;
    }
    if (ch < 0x20) return 1;           /* CR, BS, ESC, ... */
    if (g_line_len < SPLASH_LINE - 1)
        g_line[g_line_len++] = (char)ch;
    return 1;                          /* suppress: screen is the splash's */
}

/* ---- public API ------------------------------------------------------- */

int boot_splash_active(void) {
    return g_splash_on;
}

void boot_splash_begin(const char *cmdline) {
    g_splash_on = 0;
    g_line_len = 0;
    /* oc.splash=0 -> classic boot (the diagnostics switch) */
    if (cmdline) {
        const char *p = cmdline;
        while (*p) {
            if ((p == cmdline || p[-1] == ' ') &&
                strncmp(p, "oc.splash=0", 11) == 0)
                return;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
    }
    g_splash_stage = 0;
    g_splash_on = 1;
    screen_console_clear();
    splash_draw_bar(0);
    screen_console_get_hook(&g_hook_saved);   /* kcap chain displaced */
    screen_console_set_hook(splash_hook, NULL);
    klog_record(KLOG_INFO, "boot", "splash: boot log captured to klog (oc.splash=0 for the classic boot)");
}

void boot_splash_set_stage(int stage) {
    if (!g_splash_on) return;
    if (stage <= g_splash_stage || stage > 6) return;
    g_splash_stage = stage;
    splash_draw_bar(stage);
}

void boot_splash_finish(void) {
    /* K-08 regression fix (WP-severe-fix-and-closure): a NO-SPLASH boot
     * (oc.splash=0, the /NOSPLASH.TXT diagnostics switch) must not
     * touch the console hook chain here.  boot_splash_begin() returned
     * before saving g_hook_saved, so the restore below would install
     * the zero-initialized {NULL, NULL} pair - wiping the kcap -> serial
     * tee chain and killing the serial transcript for the whole
     * session (live-caught by the 31/31 gate on the diagnostic ISO:
     * the boot ran to "L1 isolation verified" and ush's banner + first
     * prompt vanished - ush itself healthy behind a dead console).
     * The splash is only ours to finish when it was ours to begin. */
    if (!g_splash_on) return;
    /* 100% + the handoff caption, then release the screen: the caller
     * runs AFTER the L1 launcher's own prints and BEFORE ush draws its
     * prompt (the L1_READY one-shot point). */
    g_splash_stage = 6;
    splash_draw_bar(6);
    /* RESTORE the displaced chain (kcap -> serial tee), never NULL:
     * the hook slot is a stack, clearing it would kill the serial
     * transcript for the whole session. */
    screen_console_set_hook(g_hook_saved.fn, g_hook_saved.ctx);
    screen_console_clear();
    g_splash_on = 0;
    klog_record(KLOG_INFO, "boot", "splash: complete - console handed to L1");
}

void boot_splash_abort(void) {
    /* The panic path: tear the splash down FIRST so the crash report
     * lands on a full-screen console (the whole point of the abort). */
    if (!g_splash_on) return;
    g_line[g_line_len] = 0;
    if (g_line_len > 0) klog_record(KLOG_INFO, "boot", g_line);
    g_line_len = 0;
    screen_console_set_hook(g_hook_saved.fn, g_hook_saved.ctx);  /* restore, never clear */
    screen_console_clear();
    g_splash_on = 0;
    klog_record(KLOG_INFO, "boot", "splash: aborted - full-screen diagnostics");
}
