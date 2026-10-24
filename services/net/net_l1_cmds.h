/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/net command surface (exported bodies,
 * migrated by declaration in group O).  The command bodies live in
 * services/net/net_core.c; this header exports them to the L1
 * dispatch table (services/net/net_l1.c).  ifconfig/ping/netstat/
 * wget are reached through the SYS_NETCMD bridge instead and ssh/
 * sshd are interactive and stay in L0. */
#ifndef OC_NET_L1_CMDS_H
#define OC_NET_L1_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* services/net/net_core.c */
int shell_cmd_arp(const char *args);
int shell_cmd_dhcp(const char *args);
int shell_cmd_dns(const char *args);
int shell_cmd_dnstest(const char *args);
int shell_cmd_firewall(const char *args);
int shell_cmd_ip(const char *args);
int shell_cmd_lspci(const char *args);
int shell_cmd_nf_test(const char *args);
int shell_cmd_route(const char *args);
int shell_cmd_tcpcc_test(const char *args);
int shell_cmd_tcpstats(const char *args);
int shell_cmd_tcptest(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_NET_L1_CMDS_H */
