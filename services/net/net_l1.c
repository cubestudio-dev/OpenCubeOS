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
#include "shell.h"

typedef int (*net_cmd_fn_t)(const char *);

static int net_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    net_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_NET_ARP:         fn = shell_cmd_arp;         break;
        case L1SVC_NET_DHCP:        fn = shell_cmd_dhcp;        break;
        case L1SVC_NET_DNS:         fn = shell_cmd_dns;         break;
        case L1SVC_NET_DNSTEST:     fn = shell_cmd_dnstest;     break;
        case L1SVC_NET_FIREWALL:    fn = shell_cmd_firewall;    break;
        case L1SVC_NET_IP:          fn = shell_cmd_ip;          break;
        case L1SVC_NET_LSPCI:       fn = shell_cmd_lspci;       break;
        case L1SVC_NET_NF_TEST:     fn = shell_cmd_nf_test;     break;
        case L1SVC_NET_ROUTE:       fn = shell_cmd_route;       break;
        case L1SVC_NET_TCPCC_TEST:  fn = shell_cmd_tcpcc_test;  break;
        case L1SVC_NET_TCPSTATS:    fn = shell_cmd_tcpstats;    break;
        case L1SVC_NET_TCPTEST:     fn = shell_cmd_tcptest;     break;
        default: return -1;
    }
    return shell_run_fn_captured(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group O: net service identity (stack + diagnostics); the
 * TCP/IP bring-up stays in kmain.  (The v1 declaration named this
 * service for the register-class interface; group O re-declares it
 * for the L1 dispatch surface.) */
OC_SERVICE_DECL_L1("net", "network service (stack + diagnostics + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   net_l1_dispatch);
