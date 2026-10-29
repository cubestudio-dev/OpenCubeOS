/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/net op surface.  The bodies
 * live in services/net/net_core.c (renamed shell_cmd_* -> net_cmd_*
 * when the oc> shell died); this header exports them to the L1
 * dispatch table (net_l1.c).  ifconfig/ping/netstat/wget are also
 * reached through SYS_NETCMD via net_netcmd_captured().  The ssh /
 * sshd commands (and their client/server bodies net_ssh.c /
 * net_sshd.c) died with the L0 user plane in WP-03 - ring-3 takeover
 * is WP-04 work. */
#ifndef OC_NET_L1_CMDS_H
#define OC_NET_L1_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* services/net/net_core.c */
int net_cmd_arp(const char *args);
int net_cmd_dhcp(const char *args);
int net_cmd_dns(const char *args);
int net_cmd_dnstest(const char *args);
int net_cmd_firewall(const char *args);
int net_cmd_ip(const char *args);
int net_cmd_lspci(const char *args);
int net_cmd_nf_test(const char *args);
int net_cmd_route(const char *args);
int net_cmd_tcpcc_test(const char *args);
int net_cmd_tcpstats(const char *args);
int net_cmd_ssh(const char *args);    /* 3.0.0-WP04 G11a */
int net_cmd_sshd(const char *args);   /* 3.0.0-WP04 G11a */
int net_cmd_tcptest(const char *args);

/* NETCMD bridge bodies (SYS_NETCMD ifconfig/ping/netstat/wget; reached
 * through net_netcmd_captured() in net_l1.c) */
int net_cmd_ifconfig(const char *args);
int net_cmd_ping(const char *args);
int net_cmd_netstat(const char *args);
int net_cmd_wget(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_NET_L1_CMDS_H */
