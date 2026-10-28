/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/net L1 surface (SYS_L1SVC cat "net"):
 * the diagnostic half of the v1 net command class.  ifconfig/ping/
 * netstat/wget reach the SAME bodies through the ush built-ins +
 * SYS_NETCMD bridge (those four stay L0-declared); ssh/sshd stay L0
 * as interactive-session commands.  Bodies live in services/net/
 * net_core.c, referenced by the op table. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "net_l1_cmds.h"
#include "core_kcap.h"
#include "lib_string.h"   /* strncpy for the NETCMD bridge */

typedef int (*net_cmd_fn_t)(const char *);

static int net_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    net_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_NET_ARP:         fn = net_cmd_arp;         break;
        case L1SVC_NET_DHCP:        fn = net_cmd_dhcp;        break;
        case L1SVC_NET_DNS:         fn = net_cmd_dns;         break;
        case L1SVC_NET_DNSTEST:     fn = net_cmd_dnstest;     break;
        case L1SVC_NET_FIREWALL:    fn = net_cmd_firewall;    break;
        case L1SVC_NET_IP:          fn = net_cmd_ip;          break;
        case L1SVC_NET_LSPCI:       fn = net_cmd_lspci;       break;
        case L1SVC_NET_NF_TEST:     fn = net_cmd_nf_test;     break;
        case L1SVC_NET_ROUTE:       fn = net_cmd_route;       break;
        case L1SVC_NET_TCPCC_TEST:  fn = net_cmd_tcpcc_test;  break;
        case L1SVC_NET_TCPSTATS:    fn = net_cmd_tcpstats;    break;
        case L1SVC_NET_TCPTEST:     fn = net_cmd_tcptest;     break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group O: net service identity (stack + diagnostics); the
 * TCP/IP bring-up stays in kmain.  (The v1 declaration named this
 * service for the register-class interface; group O re-declares it
 * for the L1 dispatch surface.) */
OC_SERVICE_DECL_L1("net", "network service (stack + diagnostics + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   net_l1_dispatch);

/* ---- SYS_NETCMD bridge ------------------------------------------------
 * 3.0.0-WP03: the syscall layer used to synthesize an oc> command
 * line ("ifconfig", "ping <host>", ...) and run it through the
 * shell's captured-execution path.  The shell is gone; the syscall
 * now calls THIS exported entry, which dispatches the same four
 * bodies through the same core_kcap capture.  (op codes are the
 * historical NETCMD_* from core_syscall.h.) */
int net_netcmd_captured(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    net_cmd_fn_t fn = 0;
    switch (op) {
        case 1 /* NETCMD_IFCONFIG */: fn = net_cmd_ifconfig; break;
        case 2 /* NETCMD_PING */: {
            /* the historical syscall built "ping <host>" from the
             * argument; the body takes the host as its args string */
            static char host[128];
            if (!arg || !arg[0]) return -1;
            strncpy(host, arg, sizeof(host) - 1);
            host[sizeof(host) - 1] = 0;
            return core_kcap_run_fn(net_cmd_ping, host, out, cap) == 0 ? 0 : -1;
        }
        case 3 /* NETCMD_NETSTAT */: fn = net_cmd_netstat; break;
        case 4 /* NETCMD_WGET */: {
            /* same: "wget <host> [port] [path]" was the composed line */
            static char wargs[256];
            if (!arg || !arg[0]) return -1;
            strncpy(wargs, arg, sizeof(wargs) - 1);
            wargs[sizeof(wargs) - 1] = 0;
            return core_kcap_run_fn(net_cmd_wget, wargs, out, cap) == 0 ? 0 : -1;
        }
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}
