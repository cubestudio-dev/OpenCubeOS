<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络栈：从网卡之门到 TLS 与 SSH

网络栈位于 `ocos/services/net/src/svc_net_core.c`（6466 行——整个
L3/L4 引擎外加命令），兄弟文件有 `svc_net_drivers.c`（NIC 框架）、
`svc_net_tcp_cc.{c,h}`（CUBIC）、`svc_net_tls.c`（2222 行）、
`svc_net_ssh.c` / `svc_net_sshd.c`、`svc_net_wl_*.c`（无线命令 +
固件供给方）。NIC 驱动家族位于 `ocos/drivers/net/`——由
[driver-model.md](driver-model.md) 覆盖；本页是它们所喂入的协议栈。

## 轮询模型

`net_start_timer()` 注册一个周期性软定时器：

```c
core_timer_register_periodic(net_timer_cb, NULL, 10)   /* net_poll every 10 ms */
```

没有 NIC 中断（每个 NIC 驱动都屏蔽自己的中断源并被轮询；MSI/
MSI-X *可编程*且受测，但没有驱动将其用于数据路径）。`net_poll()`
排空 RX 环，运行 ARP/ICMP/IP/UDP/TCP 处理，触发各定时器（RTO、
TIME_WAIT、DHCP 重试），并额外在 `net_recv` 式等待循环内从线程
上下文被调用，使阻塞系统调用不必等下一个节拍。e1000 寄存器编程
在 ACPI S3 恢复后被重放（`net_e1000_resume`）。

## 分层

| 层 | 事实 |
|---|---|
| Ethernet | `eth_hdr_t` packed；`ETH_FRAME_MAX 1514`；类型 ARP 0x0806 / IP 0x0800 |
| NIC 驱动 | e1000 legacy 环 + virtio-net 队列内建于核心；WP-10b 框架追加 8 个 PCI 家族 + 无线（经 `wl_nic_bridge_register()` 桥接进来） |
| ARP | `g_arp_cache[16]`；`net_arp_resolve/refresh/list` |
| 路由 | `g_routes[16]`；`route_add/del/list`；单 NIC 路由（首个探测到卡的获胜） |
| IP | `net_ip_hdr_t`；**分片重组**：`IP_FRAG_MAX_FRAGS 8`，缓冲 8x1480，`IP_FRAG_TIMEOUT_TICKS 5 s`；重组/丢弃/逐出统计（BUG-0210） |
| Netfilter | `g_nf_rules[32]`，conntrack `g_nf_ct[32]`，`NF_CT_TIMEOUT 600` 节拍（60 秒）；链 INPUT/OUTPUT/FORWARD；动作 ACCEPT/DROP/REJECT(+ICMP)；状态 NEW/ESTABLISHED；每规则 `hits` 计数器 |
| ICMP | `net_icmp_hdr_t`；带超时的 echo ping（`net_icmp_ping(ip, 3000)`） |
| UDP | `g_udp_handlers[32]`；bind/unbind/send |
| TCP | 重头戏——见下 |
| 套接字 | `MAX_SOCKETS 32`，`net_socket_t`；SOCK_TCP 1 / SOCK_UDP 2；表在堆上（0x400000 窗口纪律） |
| DHCP | 端口 68/67；选项 53（消息类型）、54（服务器 id，在 REQUEST 中回显——WP-10-tui-qa 修复）、1（掩码）、3（网关）、6（DNS）；魔数 0x63825363；xid 0x12345678；DISCOVER->OFFER->REQUEST->ACK 带超时 |
| DNS | `net_dns_hdr_t`；记录类型 A、CNAME、AAAA、MX（`DNS_MAX_MX 4`）、TXT、NS、SRV；端口 53 |
| HTTP | 位于 `net_cmd_wget` 内：经 TCP 的普通 GET，响应在 `\r\n\r\n` 处切分，存入 VFS 或打印 |
| HTTPS | `wget https://...` -> `net_tls_https_get()`；16 KiB 响应缓冲（WP-09-FIX BUG-022：过去是 4 KiB 截断） |

## TCP——连接引擎

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

可靠性机制：由 srtt/rttvar 得出 RTO 并遵循 **Karn 规则**（重传的
段绝不更新 RTT）、3 重复 ACK 快速重传 + 快速恢复、**SACK**（3 块、
每 256 字节的重传图）、4 段乱序缓存、TIME_WAIT 限定 10 秒（按比例
缩小的 2*MSL）与 60 秒 FIN 族滞留预算（#28——过去 32 个死同伴会
永久耗尽该表）。`net_tcp_send` 以全有或全无方式接收进 rtx 缓冲
（将阻塞 = -1，BUG-0205）；Nagle + 对端窗口 + cwnd 把守实际发送。

32 连接表（约 520 KiB）在 `net_tcp_init` 时 kmalloc——链接器
ASSERT 把内核镜像钉在 0x400000 之下（见 [memory.md](memory.md)）。

### CUBIC 拥塞控制（RFC 8312，整数数学）

`svc_net_tcp_cc.{h,c}` 中的纯函数——常量 `CC_CUBIC_C 410`
（0.4 << 10）、`CC_CUBIC_BETA 7`/`CC_CUBIC_BETA_DEN 10`（beta =
0.7）：

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

外加 `integer_cbrt()` 与 `net_tcp_cc_on_ack/on_rto/ on_fast_recovery_enter`。`tcpstats` 转储每连接内部状态：`state= cwnd= ssthresh= mss= rto= rtx_len= dup_acks= ts= on/off cc=cubic w_max= sack=on/off fr=0/1 srtt= rttvar=`。

## 统计——netstat 数字从哪来

`net_stats_t`（`netstat` 命令的后备）：

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

`netstat` 打印这些，随后遍历套接字表（类型 + 远端 ip:port）并转储
ARP 缓存。BUG-035 修复把 e1000 寄存器转储从 netstat 中移除（迁到
每 NIC 的 `print_state` 命令）。

## TLS 1.3 / 1.2 客户端（`svc_net_tls.c`）

- **TLS 1.3（RFC 8446）**：X25519 + AES-128/256-GCM +
  ChaCha20-Poly1305，完整 HKDF 密钥调度、加密握手、
  CertificateVerify。转录缓冲 32768，溢出被**标记**（BUG-0078）；
  告警捕获 + 截断检测（BUG-0228）。
- **TLS 1.2**：ECDHE_RSA AES-GCM（0xC02F/0xC030）、ChaCha20
  （0xCCA8）、legacy DHE AES-CBC（0x0067）回退；带 RFC 7507 SCSV
  的 **TLS12 回退重试**（BUG-0233）。
- **X.509**：对照内嵌根 CA 库做链验证（`svc_crypto_roots.h`，由
  `tools/embed_roots.py` 生成——12 个根：ISRG X1/X2、GlobalSign、
  GTS R1/R4、DigiCert、Baltimore、Amazon、Microsoft、Sectigo
  E46/R46）+ SAN dNSName 主机名检查。
- 随机性来自 HMAC_DRBG
  （[hidden-mechanisms.md](hidden-mechanisms.md)、
  [../internals/drbg-crypto.md](../internals/drbg-crypto.md)）。

## SSH 客户端与服务器

- **客户端**（`svc_net_ssh.c`，SSH-2.0）：KEX 首选
  curve25519-sha256 / 回退 diffie-hellman-group14-sha256；主机密钥
  rsa-sha2-256/512 + ssh-rsa；aes128-ctr 首选 / aes128-cbc 回退；
  hmac-sha2-256；密码或公钥（无 `/etc/ssh_client_key` -> 密码重试，
  BUG-0075）；channel open + exec；TOFU 指纹。默认：端口 2222，
  用户/密码 "oc"；密码 "-" = 公钥。
- **服务器**（`svc_net_sshd.c`）：一个会话后退出；Ctrl+C 取消
  accept 等待（cancel-fn 模式，BUG-0132）。

## L1 面

Cat "net"（14 个 op）：`arp`、`dhcp`、`dns <host>`、`dnstest`、
`firewall [...]`、`ip`、`lspci`、`nf_test`、`route`、`tcpcc_test`、
`tcpstats`、`tcptest`、`ssh`、`sshd`。Cat "nic"（20 个 op）：按
家族的状态 + `*_test` 成对命令、`wifi [status|scan|...]`、`bt`。
来自 `svc_net_l1.c` 的路由说明：ifconfig/ping/netstat/wget 保持
L0 声明，经 ush 内建命令 + `SYS_NETCMD` 桥
（`net_netcmd_captured`，代码 1-4）触达**同一批**函数体。
**op 14 sshd 走直通而非捕获**——“其引擎日志（net_sshd_log）必须
实时抵达控制台/串口……op 文本保持为空”（kcap 约束——见
[hidden-mechanisms.md](hidden-mechanisms.md)）。

关键错误串（grep 锚点）：`"cannot resolve host"`、`"DNS resolution failed (timeout)"`、`"DHCP failed (timeout)"`、
`"socket failed"`、`"connect failed"`、`"wget: cannot create file, printing to console:"`、`"out of memory"`、`"TLS failed (code N)"`、
`"rule not found"`。

面向用户的指南：[../manual/network.md](../manual/network.md)；
驱动家族：[../drivers/network.md](../drivers/network.md)、
[../drivers/wireless.md](../drivers/wireless.md)。
