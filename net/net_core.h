/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-06
 * File: kernel/net.h
 * Purpose: Network stack - e1000 driver + TCP/IP + socket API + shell commands.
 *
 * This file provides the full WP-06 network stack:
 *   - PCI bus driver (in pci.c)
 *   - e1000 NIC driver
 *   - Ethernet frame layer
 *   - ARP (IP->MAC resolution)
 *   - IP (send/receive)
 *   - ICMP (ping)
 *   - UDP (send/receive, port demux)
 *   - TCP (3-way handshake, data, 4-way teardown)
 *   - DHCP client
 *   - DNS client
 *   - Socket API
 *   - Shell commands: ifconfig, ip, route, ping, netstat, dhcp, dns, wget
 */
#ifndef OC_NET_H
#define OC_NET_H

#include "types.h"

/* ---- Byte order conversion ---- */
#define htons(x) __builtin_bswap16(x)
#define ntohs(x) __builtin_bswap16(x)
#define htonl(x) __builtin_bswap32(x)
#define ntohl(x) __builtin_bswap32(x)

/* ---- Constants ---- */
#define ETH_ADDR_LEN    6
#define ETH_FRAME_MAX   1514
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IP     0x0800

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

#define TCP_SYN  0x02
#define TCP_ACK  0x10
#define TCP_FIN  0x01
#define TCP_RST  0x04
#define TCP_PSH  0x08

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DNS_PORT         53

/* ---- IP address helpers ---- */
#define IP4(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(u32)(d))

/* ---- Initialization ---- */
void net_init(void);

/* Poll for packets and process them. Call periodically (every 10ms). */
void net_poll(void);

/* Start a periodic timer that calls net_poll() every 10ms. */
void net_start_timer(void);

/* Register network shell commands (ifconfig, ping, etc.). */
void net_register_shell_commands(void);

/* ---- Network configuration ---- */
u32  net_get_ip(void);
u32  net_get_mask(void);
u32  net_get_gateway(void);
u32  net_get_dns(void);
void net_set_ip(u32 ip, u32 mask, u32 gateway);
void net_set_dns(u32 dns);
void net_get_mac(u8 mac[6]);
int  net_get_link_status(void);

/* WP-09-fix5: parse a dotted-quad IPv4 string ("10.0.2.2").  Returns the
 * host-order address, or 0 when the string is not a valid dotted quad
 * (callers then fall back to net_dns_resolve for hostnames). */
u32  net_parse_ip(const char *s);

/* ---- ARP ---- */
int net_arp_resolve(u32 ip, u8 *mac_out);
int net_arp_get_cache(int index, u32 *ip, u8 *mac);

/* ---- ICMP (ping) ---- */
int net_icmp_ping(u32 dst_ip, int timeout_ms);

/* ---- UDP ---- */
typedef void (*net_udp_handler_fn)(u32 src_ip, u16 src_port, const void *data, int len);
int net_udp_bind(u16 port, net_udp_handler_fn handler);
int net_udp_unbind(u16 port);   /* BUG-0070 */
int net_udp_send(u32 dst_ip, u16 dst_port, u16 src_port, const void *data, int len);

/* ---- TCP ---- */
typedef void (*net_tcp_handler_fn)(u32 ip, u16 port);
int net_tcp_connect(u32 dst_ip, u16 dst_port);
int net_tcp_send(int sock, const void *data, int len);
int net_tcp_close(int sock);
int net_tcp_listen(u16 port, net_tcp_handler_fn handler);

/* ---- Socket API ---- */
#define SOCK_TCP 1
#define SOCK_UDP 2

int net_socket(int type);
int net_bind(int fd, u32 ip, u16 port);
int net_connect(int fd, u32 ip, u16 port);
int net_accept(int listen_fd, u32 *client_ip, u16 *client_port);
int net_send(int fd, const void *data, int len);
int net_recv(int fd, void *buf, int len);
int net_close(int fd);

/* Returns 1 when the TCP connection behind socket fd is in the
 * ESTABLISHED state (safe to send/receive), 0 otherwise -- including
 * invalid/closed sockets and non-TCP sockets.  Callers that only need a
 * best-effort close (e.g. net_tls_close) use this to skip writes that are
 * guaranteed to fail after the peer has sent FIN (CLOSE_WAIT). */
int net_tcp_established(int fd);

/* ---- DHCP ---- */
int net_dhcp_discover(void);

/* ---- DNS ---- */
int net_dns_resolve(const char *name, u32 *net_ip_out);
/* WP-09: DNS CNAME + AAAA */
int net_dns_resolve_cname(const char *name, char *cname_out, int cname_len, u32 *net_ip_out);
int net_dns_resolve_aaaa(const char *name, u8 *ipv6_out);
/* WP-09 mainstream: MX / TXT / NS / SRV */
int net_dns_resolve_mx(const char *name, u16 *pref_out, char *host_out,
                   int host_stride, int max);
int net_dns_resolve_txt(const char *name, char *txt_out, int txt_len);
int net_dns_resolve_ns(const char *name, char *ns_out, int ns_len);
int net_dns_resolve_srv(const char *name, u16 *pri, u16 *wgt, u16 *port,
                    char *target_out, int target_len);

/* ---- WP-09 mainstream: Netfilter (firewall) ---- */
#define NF_CHAIN_INPUT   0
#define NF_CHAIN_OUTPUT  1
#define NF_CHAIN_FORWARD 2
#define NF_CHAIN_COUNT   3
#define NF_ACTION_ACCEPT 0
#define NF_ACTION_DROP   1
#define NF_ACTION_REJECT 2   /* drop + ICMP dest-unreachable (INPUT) */
/* connection-tracking states a rule can match on */
#define NF_STATE_ANY         0xFF
#define NF_STATE_NEW         0
#define NF_STATE_ESTABLISHED 1

typedef int (*net_netfilter_hook_fn)(u8 chain, u32 src_ip, u32 dst_ip, u8 protocol, u16 port);

typedef struct {
    u32 src_ip, src_mask;
    u32 dst_ip, dst_mask;
    u8  protocol;      /* 0 = any */
    u16 port;          /* matches src OR dst port; 0 = any */
    u8  chain;
    u8  action;
    u8  state;         /* NF_STATE_* ; 0xFF = any */
    u64 hits;          /* packets matched by this rule */
    int in_use;
} net_netfilter_rule_t;

void net_netfilter_register_hook(net_netfilter_hook_fn fn);
int net_netfilter_add_rule(u8 chain, u32 src_ip, u32 src_mask, u32 dst_ip, u32 dst_mask,
                       u8 protocol, u16 port, u8 action);
/* Mainstream variants: stateful rule + per-chain default policy. */
int net_netfilter_add_rule_st(u8 chain, u32 src_ip, u32 src_mask, u32 dst_ip, u32 dst_mask,
                          u8 protocol, u16 port, u8 action, u8 state);
int net_netfilter_del_rule(int index);
int net_netfilter_list_rules(net_netfilter_rule_t *out, int max);
void net_netfilter_set_policy(u8 chain, u8 action);
u8  net_netfilter_get_policy(u8 chain);
void net_netfilter_reset(void);
/* Connection tracking (L0 conntrack): classify a flow as NEW or ESTABLISHED.
 * Called by the INPUT/OUTPUT hooks; the table also feeds `firewall ct`. */
u8  net_netfilter_ct_classify(u8 protocol, u32 src_ip, u16 src_port,
                          u32 dst_ip, u16 dst_port, int outbound,
                          u16 net_tcp_flags);
int net_netfilter_ct_count(void);
void net_netfilter_ct_flush(void);

/* ---- WP-09: Routing ---- */
typedef struct {
    u32 dst;
    u32 mask;
    u32 gateway;
    int in_use;
} route_entry_t;
int route_add(u32 dst, u32 mask, u32 gateway);
int route_del(u32 dst, u32 mask);
int route_list(route_entry_t *out, int max);

/* ---- WP-09: ARP ---- */
typedef struct {
    u32 ip;
    u8  mac[6];
    int valid;
    u64 timestamp;
} net_arp_entry_t;
int net_arp_refresh(u32 ip);
int net_arp_list(net_arp_entry_t *out, int max);

/* ---- Stats ---- */
typedef struct {
    u64 tx_packets;
    u64 rx_packets;
    u64 tx_bytes;
    u64 rx_bytes;
    u64 net_arp_requests;
    u64 net_arp_replies;
    u64 net_icmp_echo_sent;
    u64 net_icmp_echo_recv;
    u64 net_tcp_connections;
    u64 net_netfilter_drop;        /* WP-09 mainstream: netfilter drop counter */
    u64 net_netfilter_reject;      /* netfilter reject counter */
    u64 net_netfilter_forward;     /* packets seen on the FORWARD chain */
    u64 net_rx_bad_checksum;       /* BUG-0072: RX datagrams dropped on a
                                      failing checksum */
} net_stats_t;

void net_get_stats(net_stats_t *out);

#endif /* OC_NET_H */
