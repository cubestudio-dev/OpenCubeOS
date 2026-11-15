<!-- SPDX-License-Identifier: Apache-2.0 -->
# Network stack: from the NIC door to TLS and SSH

The stack lives in `ocos/services/net/src/svc_net_core.c` (6466 lines
- the whole L3/L4 engine plus commands), with siblings
`svc_net_drivers.c` (the NIC framework), `svc_net_tcp_cc.{c,h}` (CUBIC),
`svc_net_tls.c` (2222 lines), `svc_net_ssh.c` / `svc_net_sshd.c`,
`svc_net_wl_*.c` (wireless commands + firmware provider). NIC driver
families live in `ocos/drivers/net/` - [driver-model.md](driver-model.md)
covers them; this page is the protocol stack they feed.

## Polling model

`net_start_timer()` registers a periodic soft timer:

```c
core_timer_register_periodic(net_timer_cb, NULL, 10)   /* net_poll every 10 ms */
```

There are no NIC interrupts (every NIC driver masks its interrupt
source and is polled; MSI/MSI-X is *programmable* and tested but no
driver uses it for its data path). `net_poll()` drains RX rings, runs
ARP/ICMP/IP/UDP/TCP processing, fires timers (RTO, TIME_WAIT, DHCP
retry) and is additionally called from thread context inside
`net_recv`-style wait loops so a blocking syscall does not have to
wait for the next tick. The e1000 register programming is replayed
after ACPI S3 resume (`net_e1000_resume`).

## Layers

| Layer | Facts |
|---|---|
| Ethernet | `eth_hdr_t` packed; `ETH_FRAME_MAX 1514`; types ARP 0x0806 / IP 0x0800 |
| NIC drivers | e1000 legacy rings + virtio-net queues built into the core; the WP-10b framework adds 8 PCI families + wireless (bridged in via `wl_nic_bridge_register()`) |
| ARP | `g_arp_cache[16]`; `net_arp_resolve/refresh/list` |
| Routing | `g_routes[16]`; `route_add/del/list`; single-NIC routing (first probed card wins) |
| IP | `net_ip_hdr_t`; **fragment reassembly**: `IP_FRAG_MAX_FRAGS 8`, buffer 8x1480, `IP_FRAG_TIMEOUT_TICKS 5 s`; stats for reassembled/dropped/evicted (BUG-0210) |
| Netfilter | `g_nf_rules[32]`, conntrack `g_nf_ct[32]`, `NF_CT_TIMEOUT 600` ticks (60 s); chains INPUT/OUTPUT/FORWARD; actions ACCEPT/DROP/REJECT(+ICMP); states NEW/ESTABLISHED; per-rule `hits` counter |
| ICMP | `net_icmp_hdr_t`; echo ping with timeout (`net_icmp_ping(ip, 3000)`) |
| UDP | `g_udp_handlers[32]`; bind/unbind/send |
| TCP | the big one - below |
| Sockets | `MAX_SOCKETS 32`, `net_socket_t`; SOCK_TCP 1 / SOCK_UDP 2; table on the heap (the 0x400000 window discipline) |
| DHCP | ports 68/67; options 53 (msg type), 54 (server id, echoed in REQUEST - the WP-10-tui-qa fix), 1 (mask), 3 (gw), 6 (dns); magic 0x63825363; xid 0x12345678; DISCOVER->OFFER->REQUEST->ACK with timeout |
| DNS | `net_dns_hdr_t`; record types A, CNAME, AAAA, MX (`DNS_MAX_MX 4`), TXT, NS, SRV; port 53 |
| HTTP | inside `net_cmd_wget`: plain GET over TCP, response split at `\r\n\r\n`, saved to VFS or printed |
| HTTPS | `wget https://...` -> `net_tls_https_get()`; 16 KiB response buffer (WP-09-FIX BUG-022: was 4 KiB truncation) |

## TCP - the connection engine

```c
#define TCP_MAX_CONNS 32
typedef enum {
    TCP_CLOSED = 0, TCP_SYN_SENT, TCP_SYN_RCVD, TCP_ESTABLISHED,
    TCP_FIN_WAIT_1, TCP_FIN_WAIT_2, TCP_CLOSE_WAIT, TCP_LAST_ACK,
    TCP_CLOSING, TCP_TIME_WAIT,   /* BUG-0208: active-close terminal state, 2*MSL-bounded */
} net_tcp_state_t;

#define TCP_TIME_WAIT_TICKS (10u * OC_TIMER_HZ)   /* 10 s @ 100 Hz */
#define TCP_FIN_TIMEOUT_TICKS (60u * OC_TIMER_HZ)  /* 60 s @ 100 Hz */
#define TCP_RTX_BUF_SIZE 4096
#define TCP_OOO_MAX 4
#define TCP_OOO_SEG 1024

typedef struct {
    net_tcp_state_t state;
    u32 remote_ip;
    u16 local_port;
    u16 remote_port;
    u32 our_seq;        /* next seq to send */
    u32 our_ack;        /* ack we send to peer */
    u32 driver_snd_una;        /* oldest unacknowledged seq (WP-09) */
    u32 driver_snd_wnd;        /* peer's advertised window (WP-09) */
    u32 cwnd;           /* congestion window (WP-09) */
    u32 ssthresh;       /* slow-start threshold (WP-09) */
    u32 rto;            /* retransmission timeout in ticks (WP-09) */
    u64 rto_deadline;   /* timer tick when RTO fires (WP-09) */
    int dup_ack_count;  /* duplicate ACK counter (WP-09) */
    u16 mss;            /* maximum segment size (WP-09) */
    u8  win_scale_sent; /* window scale we offered (WP-09) */
    u8  win_scale_recv; /* window scale peer offered (WP-09) */
    u32 ts_recent;      /* latest timestamp from peer (WP-09) */
    u32 ts_echo;        /* timestamp to echo back (WP-09) */
    int ts_enabled;     /* timestamps negotiated (WP-09) */
    int in_use;
    int sock_fd;
    u8  rx_buf[8192];   /* WP-09-FIX BUG-002: was 2048 (< two MSS) */
    int rx_len;
    u8  rtx_buf[TCP_RTX_BUF_SIZE];
    int rtx_len;         /* bytes in rtx_buf awaiting ACK */
    int rtx_sent;        /* [0,rtx_sent) in flight; [rtx_sent,rtx_len) queued (Nagle/window/cwnd) */
    u32 rtx_seq;         /* seq of first byte in rtx_buf */
    int rtt_measured;    /* have we measured RTT yet */
    u32 srtt;            /* smoothed RTT in ticks */
    u32 rttvar;          /* RTT variance */
    u64 send_tick;       /* tick when the oldest unacked data was sent */
    int rtx_retransmitted; /* oldest unacked data was re-sent (Karn: no RTT) */
    u8  rtx_sacked[TCP_RTX_BUF_SIZE / 256];
    u8  sack_permitted;  /* peer negotiated SACK-Permitted */
    u8  fr_active;       /* fast recovery in progress */
    u32 sack_l[3];
    u32 sack_r[3];
    int sack_count;
    cubic_state_t cc;    /* CUBIC state (RFC 8312) */
    u32 ooo_seq[TCP_OOO_MAX];
    u16 ooo_len[TCP_OOO_MAX];
    u8  ooo_data[TCP_OOO_MAX][TCP_OOO_SEG];
    int ooo_count;
} net_tcp_conn_t;
```

Reliability machinery: RTO from srtt/rttvar with **Karn's rule**
(retransmitted segments never update RTT), 3-duplicate-ACK fast
retransmit + fast recovery, **SACK** (3 blocks, per-256-byte
retransmit map), a 4-segment out-of-order cache, TIME_WAIT bounded to
10 s (the scaled-down 2*MSL) and a 60 s FIN-family linger budget
(#28 - 32 dead peers used to exhaust the table forever). `net_tcp_send`
accepts into the rtx buffer all-or-nothing (would-block = -1,
BUG-0205); Nagle + peer window + cwnd gate actual transmission.

The 32-connection table (~520 KiB) is kmalloc'd at `net_tcp_init` -
the linker ASSERT pins the kernel image below 0x400000
([memory.md](memory.md)).

### CUBIC congestion control (RFC 8312, integer math)

Pure functions in `svc_net_tcp_cc.{h,c}` - constants
`CC_CUBIC_C 410` (0.4 << 10), `CC_CUBIC_BETA 7`/`CC_CUBIC_BETA_DEN 10`
(beta = 0.7):

```c
typedef struct {
    u32 w_max;        /* pre-congestion window (bytes) */
    u32 w_last_max;   /* previous W_max for fast convergence (bytes) */
    u64 epoch_start;  /* tick when the current epoch began (0 = start now) */
    u32 k_ticks;      /* time to reach w_max after the epoch starts */
    u32 net_tcp_cwnd;     /* Reno-friendly window estimate (bytes) */
    u32 net_tcp_acks;     /* ACK byte accumulator for Reno-friendly growth */
} cubic_state_t;
```

plus `integer_cbrt()` and `net_tcp_cc_on_ack/on_rto/ on_fast_recovery_enter`. `tcpstats` dumps the per-connection
internals: `state= cwnd= ssthresh= mss= rto= rtx_len= dup_acks= ts= on/off cc=cubic w_max= sack=on/off fr=0/1 srtt= rttvar=`.

## Stats - where netstat's numbers come from

`net_stats_t` (the backing of the `netstat` command):

```c
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
    u64 net_rx_bad_checksum;       /* BUG-0072: RX datagrams dropped on a failing checksum */
    u64 net_udp_rx_dropped;        /* BUG-0217: UDP datagrams dropped because the bound socket's rx buffer was full */
    u64 net_ip_frag_rx_dropped;    /* BUG-0210: IP fragments rejected */
    u64 net_ip_frag_evicted;       /* BUG-0210: fragment-reassembly slots reclaimed */
    u64 net_ip_frag_reassembled;   /* BUG-0210: fragmented datagrams reassembled */
    u64 tx_errors;                 /* #27: legacy e1000 TX attempts that failed the DD wait or reported EC/LC */
} net_stats_t;
```

`netstat` prints these, then walks the socket table (type + remote
ip:port) and dumps the ARP cache. The BUG-035 fix removed the e1000
register dump from netstat (it moved to the per-NIC `print_state`
commands).

## TLS 1.3 / 1.2 client (`svc_net_tls.c`)

- **TLS 1.3 (RFC 8446)**: X25519 + AES-128/256-GCM +
  ChaCha20-Poly1305, the full HKDF key schedule, encrypted handshake,
  CertificateVerify. Transcript buffer 32768 with overflow **flagged**
  (BUG-0078); alert capture + truncation detection (BUG-0228).
- **TLS 1.2**: ECDHE_RSA AES-GCM (0xC02F/0xC030), ChaCha20 (0xCCA8),
  legacy DHE AES-CBC (0x0067) fallback; **TLS12 fallback retry** with
  the RFC 7507 SCSV (BUG-0233).
- **X.509**: chain verification against the embedded root CA store
  (`svc_crypto_roots.h`, generated by `tools/embed_roots.py` - 12
  roots: ISRG X1/X2, GlobalSign, GTS R1/R4, DigiCert, Baltimore,
  Amazon, Microsoft, Sectigo E46/R46) + SAN dNSName hostname check.
- Randomness from the HMAC_DRBG
  ([hidden-mechanisms.md](hidden-mechanisms.md),
  [../internals/drbg-crypto.md](../internals/drbg-crypto.md)).

## SSH client and server

- **Client** (`svc_net_ssh.c`, SSH-2.0): KEX curve25519-sha256
  preferred / diffie-hellman-group14-sha256 fallback; host keys
  rsa-sha2-256/512 + ssh-rsa; aes128-ctr preferred / aes128-cbc
  fallback; hmac-sha2-256; password or publickey (no
  `/etc/ssh_client_key` -> password retry, BUG-0075); channel open +
  exec; TOFU fingerprint. Defaults: port 2222, user/pass "oc";
  password "-" = publickey.
- **Server** (`svc_net_sshd.c`): one session then exit; Ctrl+C
  cancels the accept wait (the cancel-fn pattern, BUG-0132).

## The L1 surface

Cat "net" (14 ops): `arp`, `dhcp`, `dns <host>`, `dnstest`,
`firewall [...]`, `ip`, `lspci`, `nf_test`, `route`, `tcpcc_test`,
`tcpstats`, `tcptest`, `ssh`, `sshd`. Cat "nic" (20 ops): per-family
status + `*_test` pairs, `wifi [status|scan|...]`, `bt`. Routing
notes from `svc_net_l1.c`: ifconfig/ping/netstat/wget stay L0-declared
and reach the SAME bodies through ush built-ins + the `SYS_NETCMD`
bridge (`net_netcmd_captured`, codes 1-4). **op 14 sshd routes
DIRECT, not captured** - "its engine logs (net_sshd_log) must reach
the console/serial LIVE... the op text stays empty" (the kcap
constraint - [hidden-mechanisms.md](hidden-mechanisms.md)).

Key error strings (grep anchors): `"cannot resolve host"`, `"DNS resolution failed (timeout)"`, `"DHCP failed (timeout)"`, `"socket failed"`, `"connect failed"`, `"wget: cannot create file, printing to console:"`, `"out of memory"`, `"TLS failed (code N)"`,
`"rule not found"`.

User-facing guide: [../manual/network.md](../manual/network.md);
driver families: [../drivers/network.md](../drivers/network.md),
[../drivers/wireless.md](../drivers/wireless.md).
