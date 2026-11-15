/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "sysdiag" (the system
 * diagnostics mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   sysdiag                -> the menu wizard below
 *   sysdiag direct <op> .. -> explicit verbatim passthrough
 *   sysdiag <op> [args]    -> classic verbatim dispatch
 *
 * The table carries the WHOLE diag service (mem .. smbustest) plus
 * the health, klog and watchdog surfaces - 46 ops, zero
 * functionality loss; the single-command apps stay registered.
 *
 * The wizard groups by intent ("the machine feels wrong - look"):
 * vitals, kernel log, memory picture, scheduler, crash records, log
 * tools, and a selftest lab that runs the core selftests one after
 * another.  crashtest (deliberate panic injection) stays reachable
 * through classic/direct only - a wizard button for it would be a
 * footgun. */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

struct wiz_op {
    const char *name;
    long cat;
    long op;
};

static const struct wiz_op g_ops[] = {
    {"mem",        L1SVC_CAT_DIAG, L1SVC_DIAG_MEM},
    {"frag",       L1SVC_CAT_DIAG, L1SVC_DIAG_FRAG},
    {"heap",       L1SVC_CAT_DIAG, L1SVC_DIAG_HEAP},
    {"sched",      L1SVC_CAT_DIAG, L1SVC_DIAG_SCHED},
    {"syncstat",   L1SVC_CAT_DIAG, L1SVC_DIAG_SYNCSTAT},
    {"crashlog",   L1SVC_CAT_DIAG, L1SVC_DIAG_CRASHLOG},
    {"crashtest",  L1SVC_CAT_DIAG, L1SVC_DIAG_CRASHTEST},
    {"dumpinfo",   L1SVC_CAT_DIAG, L1SVC_DIAG_DUMPINFO},
    {"cr3test",    L1SVC_CAT_DIAG, L1SVC_DIAG_CR3TEST},
    {"exc",        L1SVC_CAT_DIAG, L1SVC_DIAG_EXC},
    {"pftest",     L1SVC_CAT_DIAG, L1SVC_DIAG_PFTEST},
    {"pmmrace",    L1SVC_CAT_DIAG, L1SVC_DIAG_PMMRACE},
    {"vmtest",     L1SVC_CAT_DIAG, L1SVC_DIAG_VMTEST},
    {"vmkernelpt", L1SVC_CAT_DIAG, L1SVC_DIAG_VMKERNELPT},
    {"vmmap",      L1SVC_CAT_DIAG, L1SVC_DIAG_VMMAP},
    {"memtest",    L1SVC_CAT_DIAG, L1SVC_DIAG_MEMTEST},
    {"heaptest",   L1SVC_CAT_DIAG, L1SVC_DIAG_HEAPTEST},
    {"heapbounds", L1SVC_CAT_DIAG, L1SVC_DIAG_HEAPBOUNDS},
    {"irqabitest", L1SVC_CAT_DIAG, L1SVC_DIAG_IRQABITEST},
    {"timer",      L1SVC_CAT_DIAG, L1SVC_DIAG_TIMER},
    {"msitest",    L1SVC_CAT_DIAG, L1SVC_DIAG_MSITEST},
    {"spawn",      L1SVC_CAT_DIAG, L1SVC_DIAG_SPAWN},
    {"multi",      L1SVC_CAT_DIAG, L1SVC_DIAG_MULTI},
    {"nice",       L1SVC_CAT_DIAG, L1SVC_DIAG_NICE},
    {"synctest",   L1SVC_CAT_DIAG, L1SVC_DIAG_SYNCTEST},
    {"fstest",     L1SVC_CAT_DIAG, L1SVC_DIAG_FSTEST},
    {"acpi",       L1SVC_CAT_DIAG, L1SVC_DIAG_ACPI},
    {"acpidev",    L1SVC_CAT_DIAG, L1SVC_DIAG_ACPIDEV},
    {"acpipow",    L1SVC_CAT_DIAG, L1SVC_DIAG_ACPIPOW},
    {"acpitest",   L1SVC_CAT_DIAG, L1SVC_DIAG_ACPITEST},
    {"pcidump",    L1SVC_CAT_DIAG, L1SVC_DIAG_PCIDUMP},
    {"pcibars",    L1SVC_CAT_DIAG, L1SVC_DIAG_PCIBARS},
    {"pcie",       L1SVC_CAT_DIAG, L1SVC_DIAG_PCIE},
    {"i2cdetect",  L1SVC_CAT_DIAG, L1SVC_DIAG_I2CDETECT},
    {"i2cget",     L1SVC_CAT_DIAG, L1SVC_DIAG_I2CGET},
    {"i2cset",     L1SVC_CAT_DIAG, L1SVC_DIAG_I2CSET},
    {"i2ctest",    L1SVC_CAT_DIAG, L1SVC_DIAG_I2CTEST},
    {"smbus",      L1SVC_CAT_DIAG, L1SVC_DIAG_SMBUS},
    {"smbustest",  L1SVC_CAT_DIAG, L1SVC_DIAG_SMBUSTEST},
    {"health",     L1SVC_CAT_HEALTH, L1SVC_HEALTH_SNAPSHOT},
    {"health export", L1SVC_CAT_HEALTH, L1SVC_HEALTH_EXPORT},
    {"health watch",  L1SVC_CAT_HEALTH, L1SVC_HEALTH_WATCH},
    {"dmesg",      L1SVC_CAT_KLOG, L1SVC_KLOG_DMESG},
    {"logcat",     L1SVC_CAT_KLOG, L1SVC_KLOG_LOGCAT},
    {"logctl",     L1SVC_CAT_KLOG, L1SVC_KLOG_LOGCTL},
    {"watchdog",   L1SVC_CAT_WATCHDOG, L1SVC_WDT_CMD},
};
#define G_OPS_N ((int)(sizeof(g_ops) / sizeof(g_ops[0])))

static char g_out[WIZ_OUT_CAP];

/* Two-word spellings exist in the table ("health export",
 * "health watch" - the historical subcommand forms).  Try the
 * two-word join first, then the bare word; *words gets how many
 * argv slots were consumed by the op name. */
static const struct wiz_op *find_op_at(char **argv, int start, int argc,
                                        int *words) {
    if (start + 1 < argc) {
        char two[48];
        strcpy_(two, argv[start]);
        strcat_(two, " ");
        strcat_(two, argv[start + 1]);
        for (int i = 0; i < G_OPS_N; i++)
            if (streq_(g_ops[i].name, two)) {
                *words = 2;
                return &g_ops[i];
            }
    }
    for (int i = 0; i < G_OPS_N; i++)
        if (streq_(g_ops[i].name, argv[start])) {
            *words = 1;
            return &g_ops[i];
        }
    *words = 0;
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
    puts_("usage: sysdiag            (interactive menu)\n");
    puts_("       sysdiag direct <op> [args]   (explicit passthrough)\n");
    puts_("       sysdiag <op> [args]          (classic op dispatch)\n");
    puts_("ops:");
    for (int i = 0; i < G_OPS_N; i++) {
        puts_("  ");
        puts_(g_ops[i].name);
        putc_('\n');
    }
}

/* ---- submenus -------------------------------------------------------- */
static void submenu_logs(void) {
    static const char *const items[] = {
        "sd_l_logcat", "sd_l_logctl", "sd_l_export", "sd_l_watch"};
    for (;;) {
        int c = wiz_menu(tr_("sd_l_title"), items, 4);
        if (c == 0) return;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_KLOG, L1SVC_KLOG_LOGCAT, 0, g_out);
        } else if (c == 2) {
            (void)wiz_svc(L1SVC_CAT_KLOG, L1SVC_KLOG_LOGCTL, "status", g_out);
        } else if (c == 3) {
            (void)wiz_svc(L1SVC_CAT_HEALTH, L1SVC_HEALTH_EXPORT, 0, g_out);
        } else {
            /* health watch loops until ENTER - exactly the service
             * contract, the pause below is redundant but harmless */
            (void)wiz_svc(L1SVC_CAT_HEALTH, L1SVC_HEALTH_WATCH, 0, g_out);
        }
        wiz_pause();
    }
}

static void submenu_selftest(void) {
    static const char *const items[] = {
        "sd_t_all", "sd_t_mem", "sd_t_heap", "sd_t_fs", "sd_t_vm"};
    for (;;) {
        int c = wiz_menu(tr_("sd_t_title"), items, 5);
        if (c == 0) return;
        if (c == 1) {
            /* the core four, one after another */
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_MEMTEST, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_HEAPTEST, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_FSTEST, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_VMTEST, 0, g_out);
        } else if (c == 2) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_MEMTEST, 0, g_out);
        } else if (c == 3) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_HEAPTEST, 0, g_out);
        } else if (c == 4) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_FSTEST, 0, g_out);
        } else {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_VMTEST, 0, g_out);
        }
        wiz_pause();
    }
}

/* ---- the main menu --------------------------------------------------- */
static int sysdiag_wizard(void) {
    static const char *const items[] = {
        "sd_m_health", "sd_m_dmesg", "sd_m_mem", "sd_m_sched",
        "sd_m_crash", "sd_m_logs", "sd_m_selftest"};
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 7);
        if (c == 0) return 0;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_HEALTH, L1SVC_HEALTH_SNAPSHOT, 0, g_out);
        } else if (c == 2) {
            (void)wiz_svc(L1SVC_CAT_KLOG, L1SVC_KLOG_DMESG, 0, g_out);
        } else if (c == 3) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_MEM, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_FRAG, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_HEAP, 0, g_out);
        } else if (c == 4) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_SCHED, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_SYNCSTAT, 0, g_out);
        } else if (c == 5) {
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_CRASHLOG, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_DIAG, L1SVC_DIAG_DUMPINFO, 0, g_out);
        } else if (c == 6) {
            submenu_logs();
        } else {
            submenu_selftest();
        }
        if (c != 6 && c != 7) wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return sysdiag_wizard();

    int start = 1;
    if (streq_(argv[1], "direct")) {
        if (argc < 3) { usage(); return 1; }
        start = 2;
    }
    int words = 0;
    const struct wiz_op *o = find_op_at(argv, start, argc, &words);
    if (!o) {
        puts_("sysdiag: unknown op: ");
        puts_(argv[start]);
        putc_('\n');
        usage();
        return 1;
    }
    return run_op(o, argc, argv, start + words);
}
