/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_main.c
 * Purpose: OCSE environment assembly + the GUI/CLI mode loop. Owns
 * the Ctrl+Alt+F1/F2 hotkey handler (GUI <-> CLI) and the GUI-mode
 * console suppression hook (stray kernel console output goes to the
 * serial line only, so the installer panel never gets scribbled on).
 */
#include "ocse_main.h"
#include "ocse_i18n.h"
#include "ocse_detect.h"
#include "ocse_disk.h"
#include "ocse_gui.h"
#include "ocse_cl.h"
#include "ocse_atapi.h"
#include "ocse_iso.h"
#include "fs_vfs.h"
#include "screen_console.h"
#include "screen_fb.h"
#include "screen_serial_in.h"
#include "l1_ext.h"
#include "driver_input_keyboard.h"
#include "core_power.h"
#include "lib_string.h"
#include <stdint.h>

/* Provided by kernel/main.c (the rewritten v1.0.0 serial helpers). */
void ocse_serial_puts_raw(const char* s);

/* 0 = none, 1 = switch to CLI (Ctrl+Alt+F2), 2 = switch to GUI (F1). */
int g_ocse_hotkey_req = 0;

/* Keyboard chain handler: consumes the F1/F2 combos with Ctrl+Alt
 * held. Runs for PS/2 and serial-injected keys alike (both paths
 * walk the handler chain before the ASCII queue). */
static int ocse_kbd_hotkeys(u16 key, u8 mods) {
    if ((mods & (OC_MOD_CTRL | OC_MOD_ALT)) != (OC_MOD_CTRL | OC_MOD_ALT))
        return 0;
    if (key == OC_KEY_F2) {
        ocse_serial_puts_raw("[ocse] hotkey: Ctrl+Alt+F2\r\n");
        g_ocse_hotkey_req = 1; return 1;
    }
    if (key == OC_KEY_F1) {
        ocse_serial_puts_raw("[ocse] hotkey: Ctrl+Alt+F1\r\n");
        g_ocse_hotkey_req = 2; return 1;
    }
    return 0;
}

/* GUI-mode console suppression: mirror to serial, do not draw. */
static int ocse_console_sink(void* ctx, u8 ch) {
    (void)ctx;
    if (ch == '\n') ocse_serial_puts_raw("\r\n");
    else {
        char one[2] = { (char)ch, 0 };
        ocse_serial_puts_raw(one);
    }
    return 1;                      /* consumed: keep the panel clean */
}

void ocse_main_run(const arch_multiboot2_info_t* mbi) {
    (void)mbi;
    ocse_i18n_init();

    /* WP-06b: bring up the ATAPI install medium and mount it at
     * /install (read-only). Serial-log only: a missing medium is not
     * fatal - the negative acceptance boots from the ISO with no
     * target disk and must still reach check4 FAIL -> halt. */
    {
        int cds = ocse_atapi_init();
        ocse_iso_init();
        char n[4];
        n[0] = '0' + (char)(cds > 9 ? 9 : cds); n[1] = 0;
        ocse_serial_puts_raw("[ocse] atapi drives: ");
        ocse_serial_puts_raw(n);
        ocse_serial_puts_raw("\r\n");
        int mounted = fs_vfs_mount("iso9660", "/install", "cd0");
        if (mounted == 0) {
            ocse_serial_puts_raw(
                "[ocse] install medium: /install (iso9660 cd0)\r\n");
            /* Medium self-check: the two WP-06b payloads must be
             * reachable through the VFS (case-insensitive lookup, ";1"
             * stripped) before the shell offers tar/ocbl-install. */
            fs_vfs_stat_t st;
            if (fs_vfs_stat("/install/ocos.tar", &st) == 0) {
                char n[24];
                ocse_serial_puts_raw("[ocse] medium check: ocos.tar ");
                u64_to_str(st.size, n);
                ocse_serial_puts_raw(n);
                if (fs_vfs_stat("/install/ocblmbr.bin", &st) == 0) {
                    ocse_serial_puts_raw(" B, ocblmbr.bin ");
                    u64_to_str(st.size, n);
                    ocse_serial_puts_raw(n);
                    ocse_serial_puts_raw(" B");
                } else {
                    ocse_serial_puts_raw(" B, ocblmbr.bin MISSING");
                }
                ocse_serial_puts_raw("\r\n");
            } else {
                ocse_serial_puts_raw(
                    "[ocse] medium check: ocos.tar MISSING\r\n");
            }
        }
        else
            ocse_serial_puts_raw(
                "[ocse] no ISO install medium (tar/ocbl-install need it)\r\n");
    }

    ocse_detect_run();
    ocse_disk_scan();

    /* Install-medium contract (WP-06a): an installer with no target
     * disk is pointless - report honestly and halt (the negative-case
     * acceptance: SeaBIOS, no disk, check4 FAIL, machine stops). */
    if (!g_ocse_checks[3].pass) {
        ocse_serial_puts_raw("[ocse] FATAL: no usable disk - halting\r\n");
        for (int i = 0; i < OCSE_CHECKS; i++) {
            ocse_serial_puts_raw("  [");
            ocse_serial_puts_raw(g_ocse_checks[i].pass ? "PASS" : "FAIL");
            ocse_serial_puts_raw("] ");
            ocse_serial_puts_raw(g_ocse_checks[i].detail);
            ocse_serial_puts_raw("\r\n");
        }
        core_power_halt();          /* flush + stop; never returns */
    }

    driver_input_keyboard_register_handler(ocse_kbd_hotkeys);

    /* Greppable serial summary of the four checks (the GUI shows the
     * full panel; the serial line gets the verdicts for acceptance
     * logs, positive and negative paths alike). */
    {
        static const char* const names[OCSE_CHECKS] =
            { "cpu", "mem", "disp", "disk" };
        ocse_serial_puts_raw("[ocse] checks:");
        for (int i = 0; i < OCSE_CHECKS; i++) {
            ocse_serial_puts_raw(" ");
            ocse_serial_puts_raw(names[i]);
            ocse_serial_puts_raw(g_ocse_checks[i].pass ? "=PASS" : "=FAIL");
        }
        ocse_serial_puts_raw("\r\n");
    }

    int mode_gui = 1;
    l1_ext_set_console_hook(ocse_console_sink, NULL);
    ocse_gui_init();
    ocse_serial_puts_raw("[ocse] environment up: GUI mode\r\n");

    for (;;) {
        if (mode_gui) {
            int act = ocse_gui_loop();
            if (g_ocse_hotkey_req == 1 || act == OCSE_ACT_GOTO_CL) {
                g_ocse_hotkey_req = 0;
                mode_gui = 0;
                l1_ext_set_console_hook(NULL, NULL);
                screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                                     screen_fb_rgb(0x10, 0x10, 0x14));
                ocse_outln(ocse_tr(OCSE_T_CLI_BANNER));
                ocse_outln(ocse_tr(OCSE_T_CLI_HELP));
                ocse_serial_puts_raw("[ocse] switched: CLI mode\r\n");
            } else if (act == OCSE_ACT_TOGGLE_LANG) {
                ocse_i18n_set(ocse_i18n_lang() == OCSE_LANG_EN
                              ? OCSE_LANG_ZH : OCSE_LANG_EN);
                ocse_gui_draw();
                ocse_serial_puts_raw("[ocse] language toggled\r\n");
            } else if (act == OCSE_ACT_RECHECK) {
                ocse_detect_run();
                ocse_disk_scan();
                ocse_gui_draw();
                ocse_serial_puts_raw("[ocse] re-check done\r\n");
            }
        } else {
            if (ocse_cl_loop() == OCSE_ACT_GOTO_GUI || g_ocse_hotkey_req == 2) {
                g_ocse_hotkey_req = 0;
                mode_gui = 1;
                l1_ext_set_console_hook(ocse_console_sink, NULL);
                ocse_gui_draw();
                ocse_serial_puts_raw("[ocse] switched: GUI mode\r\n");
            }
        }
    }
}
