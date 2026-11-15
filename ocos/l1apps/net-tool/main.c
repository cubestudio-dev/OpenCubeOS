/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "net-tool" (the network
 * mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   net-tool                -> the menu wizard below
 *   net-tool direct <op> .. -> explicit verbatim passthrough
 *   net-tool <op> [args]    -> classic verbatim dispatch
 *
 * The table carries the whole net service (arp .. sshd) plus the
 * wireless pair from the nic service (wifi / bt) - 16 ops, zero
 * functionality loss; the single-command apps stay registered.
 *
 * The wizard groups by user intent: address management (ip / dhcp /
 * static), name resolution, wireless (wifi + bt), perimeter
 * (firewall), paths (route + arp) and connectivity (ping / wget via
 * the SYS_NETCMD bridge - the exact syscall the historical apps
 * used).  The static-IP path collects address / mask / gateway one
 * field at a time so the op's positional argument line can never be
 * assembled in the wrong order. */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

struct wiz_op {
    const char *name;
    long cat;
    long op;
};

static const struct wiz_op g_ops[] = {
    {"arp",       L1SVC_CAT_NET, L1SVC_NET_ARP},
    {"dhcp",      L1SVC_CAT_NET, L1SVC_NET_DHCP},
    {"dns",       L1SVC_CAT_NET, L1SVC_NET_DNS},
    {"dnstest",   L1SVC_CAT_NET, L1SVC_NET_DNSTEST},
    {"firewall",  L1SVC_CAT_NET, L1SVC_NET_FIREWALL},
    {"ip",        L1SVC_CAT_NET, L1SVC_NET_IP},
    {"lspci",     L1SVC_CAT_NET, L1SVC_NET_LSPCI},
    {"nf_test",   L1SVC_CAT_NET, L1SVC_NET_NF_TEST},
    {"route",     L1SVC_CAT_NET, L1SVC_NET_ROUTE},
    {"tcpcc_test",L1SVC_CAT_NET, L1SVC_NET_TCPCC_TEST},
    {"tcpstats",  L1SVC_CAT_NET, L1SVC_NET_TCPSTATS},
    {"tcptest",   L1SVC_CAT_NET, L1SVC_NET_TCPTEST},
    {"ssh",       L1SVC_CAT_NET, L1SVC_NET_SSH},
    {"sshd",      L1SVC_CAT_NET, L1SVC_NET_SSHD},
    {"wifi",      L1SVC_CAT_NIC, L1SVC_NIC_WIFI},
    {"bt",        L1SVC_CAT_NIC, L1SVC_NIC_BT},
};
#define G_OPS_N ((int)(sizeof(g_ops) / sizeof(g_ops[0])))

static char g_out[WIZ_OUT_CAP];
static char g_nout[4096];       /* NETCMD bridge buffer (ping/wget) */

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
    puts_("usage: net-tool            (interactive menu)\n");
    puts_("       net-tool direct <op> [args]   (explicit passthrough)\n");
    puts_("       net-tool <op> [args]          (classic op dispatch)\n");
    puts_("ops:");
    for (int i = 0; i < G_OPS_N; i++) {
        puts_("  ");
        puts_(g_ops[i].name);
        putc_('\n');
    }
}

/* ---- NETCMD bridge helpers (the exact ping/wget app syscalls) ------ */
static void do_ping(void) {
    static char host[128];
    wiz_ask(tr_("nt_ask_host"), host, (int)sizeof(host));
    if (!host[0]) return;
    long rc = sys_netcmd(NETCMD_PING, host, g_nout, (long)sizeof(g_nout));
    if (rc < 0) {
        wiz_fail(tr_("wiz_svc_failed"));
        return;
    }
    puts_(g_nout);
}

static void do_wget(void) {
    static char host[128], port[16], path[160];
    wiz_ask(tr_("nt_ask_host"), host, (int)sizeof(host));
    if (!host[0]) return;
    wiz_ask(tr_("nt_ask_port"), port, (int)sizeof(port));
    wiz_ask(tr_("nt_ask_path"), path, (int)sizeof(path));
    char arg[300];
    strcpy_(arg, host);
    if (port[0]) { strcat_(arg, " "); strcat_(arg, port); }
    if (path[0]) { strcat_(arg, " "); strcat_(arg, path); }
    long rc = sys_netcmd(NETCMD_WGET, arg, g_nout, (long)sizeof(g_nout));
    if (rc < 0) {
        wiz_fail(tr_("wiz_svc_failed"));
        return;
    }
    puts_(g_nout);
}

/* ---- submenus -------------------------------------------------------- */
static void submenu_wifi(void) {
    static const char *const items[] = {
        "nt_wifi_status", "nt_wifi_scan", "nt_wifi_test", "nt_wifi_sdio"};
    for (;;) {
        int c = wiz_menu(tr_("nt_wifi_title"), items, 4);
        if (c == 0) return;
        const char *arg = (c == 1) ? 0 : (c == 2) ? "scan" :
                          (c == 3) ? "test" : "sdio";
        (void)wiz_svc(L1SVC_CAT_NIC, L1SVC_NIC_WIFI, arg, g_out);
        wiz_pause();
    }
}

static void submenu_bt(void) {
    static const char *const items[] = {
        "nt_bt_status", "nt_bt_usb", "nt_bt_uart", "nt_bt_test"};
    for (;;) {
        int c = wiz_menu(tr_("nt_bt_title"), items, 4);
        if (c == 0) return;
        const char *arg = (c == 1) ? 0 : (c == 2) ? "usb" :
                          (c == 3) ? "uart" : "test";
        (void)wiz_svc(L1SVC_CAT_NIC, L1SVC_NIC_BT, arg, g_out);
        wiz_pause();
    }
}

static void submenu_conn(void) {
    static const char *const items[] = {
        "nt_conn_ping", "nt_conn_wget", "nt_conn_ssh", "nt_conn_dnstest"};
    static char ip[64], port[16], user[32], pass[32], arg[160];
    for (;;) {
        int c = wiz_menu(tr_("nt_conn_title"), items, 4);
        if (c == 0) return;
        if (c == 1) {
            do_ping();
        } else if (c == 2) {
            do_wget();
        } else if (c == 3) {
            wiz_ask(tr_("nt_ask_ip"), ip, (int)sizeof(ip));
            if (!ip[0]) continue;
            wiz_ask(tr_("nt_ask_port_opt"), port, (int)sizeof(port));
            wiz_ask(tr_("nt_ask_user"), user, (int)sizeof(user));
            wiz_ask(tr_("nt_ask_pass"), pass, (int)sizeof(pass));
            strcpy_(arg, ip);
            if (port[0]) { strcat_(arg, " "); strcat_(arg, port); }
            if (user[0]) { strcat_(arg, " "); strcat_(arg, user); }
            if (pass[0]) { strcat_(arg, " "); strcat_(arg, pass); }
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_SSH, arg, g_out);
        } else {
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_DNSTEST, 0, g_out);
        }
        wiz_pause();
    }
}

/* ---- the main menu --------------------------------------------------- */
static int net_wizard(void) {
    static const char *const items[] = {
        "nt_m_ip", "nt_m_dhcp", "nt_m_static", "nt_m_dns",
        "nt_m_wifi", "nt_m_bt", "nt_m_fw", "nt_m_route", "nt_m_conn"};
    static char a[64], b[64], c[64], d[64];
    for (;;) {
        int m = wiz_menu(tr_("wiz_title"), items, 9);
        if (m == 0) return 0;
        if (m == 1) {
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_IP, 0, g_out);
        } else if (m == 2) {
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_DHCP, 0, g_out);
        } else if (m == 3) {
            wiz_ask(tr_("nt_static_ip"), a, (int)sizeof(a));
            if (!a[0]) continue;
            wiz_ask(tr_("nt_static_mask"), b, (int)sizeof(b));
            if (!b[0]) continue;
            wiz_ask(tr_("nt_static_gw"), c, (int)sizeof(c));
            if (!c[0]) continue;
            strcpy_(d, a); strcat_(d, " ");
            strcat_(d, b); strcat_(d, " "); strcat_(d, c);
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_IP, d, g_out);
        } else if (m == 4) {
            wiz_ask(tr_("nt_ask_host"), d, (int)sizeof(d));
            if (!d[0]) continue;
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_DNS, d, g_out);
        } else if (m == 5) {
            submenu_wifi();
        } else if (m == 6) {
            submenu_bt();
        } else if (m == 7) {
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_FIREWALL, 0, g_out);
        } else if (m == 8) {
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_ROUTE, 0, g_out);
            (void)wiz_svc(L1SVC_CAT_NET, L1SVC_NET_ARP, 0, g_out);
        } else {
            submenu_conn();
        }
        if (m != 5 && m != 6 && m != 9) wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return net_wizard();

    int start = 1;
    if (streq_(argv[1], "direct")) {
        if (argc < 3) { usage(); return 1; }
        start = 2;
    }
    const struct wiz_op *o = find_op(argv[start]);
    if (!o) {
        puts_("net-tool: unknown op: ");
        puts_(argv[start]);
        putc_('\n');
        usage();
        return 1;
    }
    return run_op(o, argc, argv, start + 1);
}
