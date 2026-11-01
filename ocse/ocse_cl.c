/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_cl.c
 * Purpose: OCSECL implementation. Line input comes from the keyboard
 * queue (both PS/2 and the COM1 serial RX path feed it); every echo
 * and command output goes through ocse_out (console + serial, zh
 * converted to GB2312 for the console renderer).
 */
#include "ocse_cl.h"
#include "ocse_gui.h"
#include "ocse_i18n.h"
#include "ocse_detect.h"
#include "ocse_disk.h"
#include "screen_console.h"
#include "screen_fb.h"
#include "driver_input_keyboard.h"
#include "core_sched.h"
#include "core_power.h"
#include "lib_string.h"
#include <stdint.h>

extern int g_ocse_hotkey_req;

#define OCSE_CL_BUF 96

static void print_checks(void) {
    for (int i = 0; i < OCSE_CHECKS; i++) {
        ocse_out("  ");
        ocse_out(ocse_tr(OCSE_T_CHECK_CPU + i));
        ocse_out(": ");
        ocse_out(ocse_tr(g_ocse_checks[i].pass ? OCSE_T_PASS : OCSE_T_FAIL));
        ocse_out("  [");
        ocse_out(g_ocse_checks[i].detail);
        ocse_outln("]");
    }
}

static void print_disks(void) {
    char n[16];
    if (g_ocse_ndisks == 0) {
        ocse_outln(ocse_tr(OCSE_T_NO_DISK));
        return;
    }
    for (int d = 0; d < g_ocse_ndisks; d++) {
        ocse_disk_t* dk = &g_ocse_disks[d];
        ocse_out("  ");
        ocse_out(dk->name);
        ocse_out(dk->table_kind == 2 ? " [GPT] " :
                 dk->table_kind == 1 ? " [MBR] " : " [no table] ");
        u64_to_str(dk->mb, n); ocse_out(n); ocse_out(" MiB, ");
        u64_to_str((u64)dk->nparts, n); ocse_out(n); ocse_out(" part(s):");
        ocse_outln("");
        for (int p = 0; p < dk->nparts; p++) {
            ocse_part_t* pt = &dk->parts[p];
            ocse_out("    #");
            u64_to_str((u64)p, n); ocse_out(n);
            ocse_out(" ");
            ocse_out(ocse_fs_name(pt->fs));
            ocse_out(" lba=");
            u64_to_str(pt->start_lba, n); ocse_out(n);
            ocse_out(" size=");
            u64_to_str(pt->mb, n); ocse_out(n); ocse_out("MiB");
            ocse_outln("");
        }
        if (dk->space_low) {
            ocse_out("    (!) ");
            ocse_outln(ocse_tr(OCSE_T_SPACE_LOW));
        }
    }
}

/* returns 1 when the caller should switch to the GUI */
static int run_command(const char* cmd) {
    if (cmd[0] == 0)
        return 0;
    if (strcmp(cmd, "help") == 0) {
        ocse_outln(ocse_tr(OCSE_T_CLI_HELP));
    } else if (strcmp(cmd, "lang") == 0) {
        ocse_outln(ocse_tr(OCSE_T_LANG_NOW));
    } else if (strncmp(cmd, "lang ", 5) == 0) {
        const char* arg = cmd + 5;
        while (*arg == ' ') arg++;
        if (strcmp(arg, "en") == 0) ocse_i18n_set(OCSE_LANG_EN);
        else if (strcmp(arg, "zh") == 0) ocse_i18n_set(OCSE_LANG_ZH);
        else { ocse_outln("usage: lang [en|zh]"); return 0; }
        ocse_outln(ocse_tr(OCSE_T_LANG_NOW));
    } else if (strcmp(cmd, "hwcheck") == 0) {
        ocse_detect_run();
        print_checks();
    } else if (strcmp(cmd, "disks") == 0) {
        ocse_disk_scan();
        print_disks();
    } else if (strcmp(cmd, "gui") == 0) {
        return 1;
    } else if (strcmp(cmd, "clear") == 0) {
        screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                             screen_fb_rgb(0x10, 0x10, 0x14));
    } else if (strncmp(cmd, "click ", 6) == 0) {
        /* Button geometry self-check (the acceptance battery drives
         * this when a real pointer device is unavailable: the QEMU
         * monitor cannot inject USB mouse events into the vendored
         * v1.0.0 USB stack). */
        int x = -1, y = -1;
        const char* a = cmd + 6;
        while (*a == ' ') a++;
        x = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
        while (*a == ' ') a++;
        y = 0;
        while (*a >= '0' && *a <= '9') y = y * 10 + (*a++ - '0');
        int act = ocse_gui_hit_test((u32)x, (u32)y);
        ocse_out("click at (");
        char n[12];
        u64_to_str((u64)x, n); ocse_out(n); ocse_out(",");
        u64_to_str((u64)y, n); ocse_out(n); ocse_out("): ");
        ocse_outln(act == OCSE_ACT_GOTO_CL ? "action=goto-cl"
                  : act == OCSE_ACT_TOGGLE_LANG ? "action=toggle-lang"
                  : act == OCSE_ACT_RECHECK ? "action=recheck"
                  : "action=none");
    } else if (strcmp(cmd, "halt") == 0) {
        ocse_outln(ocse_tr(OCSE_T_HALT));
        core_power_halt();          /* never returns */
    } else {
        ocse_out("ocse: '");
        ocse_out(cmd);
        ocse_outln("' - unknown command (try 'help')");
    }
    return 0;
}

int ocse_cl_loop(void) {
    char buf[OCSE_CL_BUF];
    int pos = 0;
    ocse_out("ocse> ");
    for (;;) {
        if (g_ocse_hotkey_req)
            return OCSE_ACT_GOTO_GUI;   /* main clears + switches */
        if (!driver_input_keyboard_has_key()) {
            core_sched_yield();
            continue;
        }
        int ch = driver_input_keyboard_getch();
        if (ch == '\r' || ch == '\n') {
            ocse_outln("");
            buf[pos] = 0;
            if (run_command(buf))
                return OCSE_ACT_GOTO_GUI;
            pos = 0;
            ocse_out("ocse> ");
        } else if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                ocse_out("\b \b");
            }
        } else if (ch >= 0x20 && ch < 0x7F && pos < OCSE_CL_BUF - 1) {
            buf[pos++] = (char)ch;
            char one[2] = { (char)ch, 0 };
            ocse_out(one);
        }
    }
}
