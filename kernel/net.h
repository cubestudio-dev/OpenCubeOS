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

/* ---- ARP ---- */
int arp_resolve(u32 ip, u8 *mac_out);
int arp_get_cache(int index, u32 *ip, u8 *mac);

/* ---- ICMP (ping) ---- */
int icmp_ping(u32 dst_ip, int timeout_ms);

/* ---- UDP ---- */
typedef void (*udp_handler_fn)(u32 src_ip, u16 src_port, const void *data, int len);
int udp_bind(u16 port, udp_handler_fn handler);
int udp_send(u32 dst_ip, u16 dst_port, u16 src_port, const void *data, int len);

/* ---- TCP ---- */
typedef void (*tcp_handler_fn)(u32 ip, u16 port);
int tcp_connect(u32 dst_ip, u16 dst_port);
int tcp_send(int sock, const void *data, int len);
int tcp_close(int sock);
int tcp_listen(u16 port, tcp_handler_fn handler);

/* ---- Socket API ---- */
#define SOCK_TCP 1
#define SOCK_UDP 2

int net_socket(int type);
int net_bind(int fd, u32 ip, u16 port);
int net_connect(int fd, u32 ip, u16 port);
int net_send(int fd, const void *data, int len);
int net_recv(int fd, void *buf, int len);
int net_close(int fd);

/* ---- DHCP ---- */
int dhcp_discover(void);

/* ---- DNS ---- */
int dns_resolve(const char *name, u32 *ip_out);

/* ---- Stats ---- */
typedef struct {
    u64 tx_packets;
    u64 rx_packets;
    u64 tx_bytes;
    u64 rx_bytes;
    u64 arp_requests;
    u64 arp_replies;
    u64 icmp_echo_sent;
    u64 icmp_echo_recv;
    u64 tcp_connections;
} net_stats_t;

void net_get_stats(net_stats_t *out);

#endif /* OC_NET_H */
