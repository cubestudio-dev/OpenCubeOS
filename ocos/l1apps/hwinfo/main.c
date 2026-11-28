/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "hwinfo" (the hardware
 * inventory mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   hwinfo                -> the menu wizard below
 *   hwinfo direct <op> .. -> explicit verbatim passthrough
 *   hwinfo <op> [args]    -> classic verbatim dispatch
 *
 * The table carries the read-only hardware surfaces: the display
 * identity ops (uname / stats / font / encoding / chinese), the four
 * clock sources (rtc / hpet / pmtimer / tsc), the PCI family
 * (lspci / pcidump / pcibars / pcie), ACPI (acpi / acpidev /
 * acpipow), the SMBus/I2C pair, USB (usb / usbdev), the camera and
 * legacy-input families, and the touchpad - every one a verbatim
 * wrap of the historical op.  Zero functionality loss; the
 * single-command apps stay registered.
 *
 * The wizard is a guided inventory tour: system summary, clocks,
 * buses, firmware tables, devices - read-only, nothing to confirm,
 * nothing to break. */
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
#include "l1_wizard_api.h"

struct wiz_op {
    const char *name;
    long cat;
    long op;
};

static const struct wiz_op g_ops[] = {
    {"uname",     L1SVC_CAT_DISPLAY, L1SVC_DISP_UNAME},
    {"stats",     L1SVC_CAT_DISPLAY, L1SVC_DISP_STATS},
    {"font",      L1SVC_CAT_DISPLAY, L1SVC_DISP_FONT},
    {"encoding",  L1SVC_CAT_DISPLAY, L1SVC_DISP_ENCODING},
    {"chinese",   L1SVC_CAT_DISPLAY, L1SVC_DISP_CHINESE},
    {"rtc",       L1SVC_CAT_TIME, L1SVC_TIME_RTC},
    {"hpet",      L1SVC_CAT_TIME, L1SVC_TIME_HPET},
    {"pmtimer",   L1SVC_CAT_TIME, L1SVC_TIME_PMTIMER},
    {"tsc",       L1SVC_CAT_TIME, L1SVC_TIME_TSC},
    {"lspci",     L1SVC_CAT_NET, L1SVC_NET_LSPCI},
    {"pcidump",   L1SVC_CAT_DIAG, L1SVC_DIAG_PCIDUMP},
    {"pcibars",   L1SVC_CAT_DIAG, L1SVC_DIAG_PCIBARS},
    {"pcie",      L1SVC_CAT_DIAG, L1SVC_DIAG_PCIE},
    {"acpi",      L1SVC_CAT_DIAG, L1SVC_DIAG_ACPI},
    {"acpidev",   L1SVC_CAT_DIAG, L1SVC_DIAG_ACPIDEV},
    {"acpipow",   L1SVC_CAT_DIAG, L1SVC_DIAG_ACPIPOW},
    {"i2cdetect", L1SVC_CAT_DIAG, L1SVC_DIAG_I2CDETECT},
    {"smbus",     L1SVC_CAT_DIAG, L1SVC_DIAG_SMBUS},
    {"usb",       L1SVC_CAT_USB, L1SVC_USB_STATUS},
    {"usbdev",    L1SVC_CAT_USB, L1SVC_USB_DEV},
    {"uvc",       L1SVC_CAT_CAMERA, L1SVC_CAM_UVC},
    {"scsi",      L1SVC_CAT_CAMERA, L1SVC_CAM_SCSI},
    {"gameport",  L1SVC_CAT_CAMERA, L1SVC_CAM_GAMEPORT},
    {"parallel",  L1SVC_CAT_CAMERA, L1SVC_CAM_PARALLEL},
    {"ir",        L1SVC_CAT_CAMERA, L1SVC_CAM_IR},
    {"touchpad",  L1SVC_CAT_INPUT, L1SVC_INP_TOUCHPAD},
};
#define G_OPS_N ((int)(sizeof(g_ops) / sizeof(g_ops[0])))

static char g_out[WIZ_OUT_CAP];

static const struct wiz_op *find_op(const char *name) {
    for (int i = 0; i < G_OPS_N; i++)
        if (streq_(g_ops[i].name, name)) return &g_ops[i];
    return (const struct wiz_op *)0;
}

static int run_op(const struct wiz_op *o, int argc, char **argv, int start) {
    char arg[256];
    int len = 0;
    arg[0] = 0;
    for (int j = start; j < argc && len < (int)sizeof(arg) - 1; j++) {
        if (j > start && len < (int)sizeof(arg) - 2) arg[len++] = ' ';
        for (const char *p = argv[j]; *p && len < (int)sizeof(arg) - 1; p++)
            arg[len++] = *p;
        arg[len] = 0;
    }
    long rc = sys_l1svc(o->cat, o->op, arg[0] ? (long)arg : 0,
                        (long)g_out, (long)sizeof(g_out));
    if (rc != 0) {
        puts_(o->name);
        puts_(tr_("svc_call_failed"));
        return 1;
    }
    puts_(g_out);
    if (g_out[0] && g_out[strlen_(g_out) - 1] != '\n') putc_('\n');
    return 0;
}

static void usage(void) {
    puts_("usage: hwinfo            (interactive menu)\n");
    puts_("       hwinfo direct <op> [args]   (explicit passthrough)\n");
    puts_("       hwinfo <op> [args]          (classic op dispatch)\n");
    puts_("ops:");
    for (int i = 0; i < G_OPS_N; i++) {
        puts_("  ");
        puts_(g_ops[i].name);
        putc_('\n');
    }
}

/* ---- submenus -------------------------------------------------------- */
static void submenu_clocks(void) {
    static const char *const items[] = {
        "hw_c_rtc", "hw_c_hpet", "hw_c_pmtimer", "hw_c_tsc"};
    static const long ops[] = {
        L1SVC_TIME_RTC, L1SVC_TIME_HPET, L1SVC_TIME_PMTIMER,
        L1SVC_TIME_TSC};
    for (;;) {
        int c = wiz_menu(tr_("hw_c_title"), items, 4);
        if (c == 0) return;
        (void)wiz_svc(L1SVC_CAT_TIME, ops[c - 1], 0, g_out);
        wiz_pause();
    }
}

static void submenu_pci(void) {
    static const char *const items[] = {
        "hw_p_lspci", "hw_p_pcidump", "hw_p_pcibars", "hw_p_pcie"};
    for (;;) {
        int c = wiz_menu(tr_("hw_p_title"), items, 4);
        if (c == 0) return;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_LSPCI, 0, g_out);
        } else {
            static const long ops[] = {
                L1SVC_DIAG_PCIDUMP, L1SVC_DIAG_PCIBARS, L1SVC_DIAG_PCIE};
            (void)wiz_svc(L1SVC_CAT_DIAG, ops[c - 2], 0, g_out);
        }
        wiz_pause();
    }
}

static void submenu_legacy(void) {
    static const char *const items[] = {
        "hw_x_uvc", "hw_x_scsi", "hw_x_gameport", "hw_x_parallel",
        "hw_x_ir", "hw_x_touchpad"};
    for (;;) {
        int c = wiz_menu(tr_("hw_x_title"), items, 6);
        if (c == 0) return;
        if (c <= 5) {
            static const long ops[] = {
                L1SVC_CAM_UVC, L1SVC_CAM_SCSI, L1SVC_CAM_GAMEPORT,
                L1SVC_CAM_PARALLEL, L1SVC_CAM_IR};
            (void)wiz_svc(L1SVC_CAT_CAMERA, ops[c - 1], 0, g_out);
        } else {
            (void)wiz_svc(L1SVC_CAT_INPUT, L1SVC_INP_TOUCHPAD, 0, g_out);
        }
        wiz_pause();
    }
}

/* ---- the main menu --------------------------------------------------- */
static int hwinfo_wizard(void) {
    static const char *const items[] = {
        "hw_m_summary", "hw_m_clocks", "hw_m_pci", "hw_m_acpi",
        "hw_m_smbus", "hw_m_usb", "hw_m_legacy", "hw_m_display"};
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 8);
        if (c == 0) return 0;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_UNAME, "-a", g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_MEM, 0, g_out);
        } else if (c == 2) {
            submenu_clocks();
        } else if (c == 3) {
            submenu_pci();
        } else if (c == 4) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_ACPI, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_ACPIDEV, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_ACPIPOW, 0, g_out);
        } else if (c == 5) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_I2CDETECT, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_SMBUS, 0, g_out);
        } else if (c == 6) {
            (void)wiz_svc(L1SVC_CAT_USB, L1SVC_USB_STATUS, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_USB, L1SVC_USB_DEV, 0, g_out);
        } else if (c == 7) {
            submenu_legacy();
        } else {
            (void)wiz_svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_STATS, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_FONT, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_ENCODING, 0, g_out);
        }
        if (c != 2 && c != 3 && c != 7) wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return hwinfo_wizard();

    int start = 1;
    if (streq_(argv[1], "direct")) {
        if (argc < 3) { usage(); return 1; }
        start = 2;
    }
    const struct wiz_op *o = find_op(argv[start]);
    if (!o) {
        puts_("hwinfo: unknown op: ");
        puts_(argv[start]);
        putc_('\n');
        usage();
        return 1;
    }
    return run_op(o, argc, argv, start + 1);
}
