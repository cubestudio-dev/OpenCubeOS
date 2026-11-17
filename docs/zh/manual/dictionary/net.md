<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 网络 (14)

### arp

| 字段 | 值 |
|---|---|
| 名称 | arp |
| 分类 | 网络 |
| 功能 | ARP 表 |
| 用法 | `arp` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
arp
ARP cache:
  (cache empty)
ush> 
```

### dhcp

| 字段 | 值 |
|---|---|
| 名称 | dhcp |
| 分类 | 网络 |
| 功能 | DHCP 租约状态/续租 |
| 用法 | `dhcp` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
dhcp
Sending DHCP discover...
DHCP failed (timeout)
ush> 
```

### dns

| 字段 | 值 |
|---|---|
| 名称 | dns |
| 分类 | 网络 |
| 功能 | 解析主机名 |
| 用法 | `dns <host>` |
| 参数 | `host` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
dns
usage: dns <name> [aaaa|cname|mx|txt|ns|srv]
ush> 
```

### dnstest

| 字段 | 值 |
|---|---|
| 名称 | dnstest |
| 分类 | 网络 |
| 功能 | DNS 解析器测试 |
| 用法 | `dnstest` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
dnstest
DNS record-type self-test (live resolver):
  [FAIL] A    example.com = 0.0.0.0
  [FAIL] AAAA google.com
  [FAIL] MX   gmail.com
  [FAIL] TXT  google.com
  [FAIL] NS   google.com
  [FAIL] SRV  _xmpp-server._tcp.jabber.org
dnstest: FAILURES
ush> 
```

### firewall

| 字段 | 值 |
|---|---|
| 名称 | firewall |
| 分类 | 网络 |
| 功能 | 包过滤规则 |
| 用法 | `firewall [...]` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
firewall
Firewall rules:
  (no rules)
policies: input=ACCEPT output=ACCEPT forward=ACCEPT
conntrack: 0 flows (firewall ct for details)
ush> 
```

### ip

| 字段 | 值 |
|---|---|
| 名称 | ip |
| 分类 | 网络 |
| 功能 | IP 地址配置 |
| 用法 | `ip` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
ip
IP: 0.0.0.0
Mask: 0.0.0.0
Gateway: 0.0.0.0
ush> 
```

### lspci

| 字段 | 值 |
|---|---|
| 名称 | lspci |
| 分类 | 网络 |
| 功能 | 列出 PCI 设备 |
| 用法 | `lspci` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
lspci
PCI devices:
  00:00.0 vendor=0x8086 device=0x1237 class=0x06
  00:01.0 vendor=0x8086 device=0x7000 class=0x06
  00:01.1 vendor=0x8086 device=0x7010 class=0x01
  00:01.3 vendor=0x8086 device=0x7113 class=0x06
  00:02.0 vendor=0x1234 device=0x1111 class=0x03
ush> 
```

### nf_test

| 字段 | 值 |
|---|---|
| 名称 | nf_test |
| 分类 | 网络 |
| 功能 | netfilter 测试 |
| 用法 | `nf_test` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
nf_test
netfilter self-test:
  [PASS] input drop rule + hits
  [PASS] state matching (est bypasses, new hits)
  [PASS] reject classification
  [PASS] per-chain default policy
  [PASS] conntrack TCP handshake
  [PASS] conntrack UDP flow
  [PASS] OUTPUT drop blocks ping (real path)
  [FAIL] rule removal restores ping
nf_test: FAILURES
ush> 
```

### route

| 字段 | 值 |
|---|---|
| 名称 | route |
| 分类 | 网络 |
| 功能 | 路由表 |
| 用法 | `route` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
route
Routing table:
  0.0.0.0 mask=0.0.0.0 gw=0.0.0.0 (default)
  (no custom routes)
ush> 
```

### ssh

| 字段 | 值 |
|---|---|
| 名称 | ssh |
| 分类 | 网络 |
| 功能 | SSH 客户端 |
| 用法 | `ssh <ip> [port] [user] [pass]` |
| 参数 | `ip` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
ssh
usage: ssh <ip> [port=2222] [user=oc] [password=oc|-]
  password "-" = publickey auth (needs /etc/ssh_client_key; falls back to password if absent)
ush> 
```

### sshd

| 字段 | 值 |
|---|---|
| 名称 | sshd |
| 分类 | 网络 |
| 功能 | SSH 服务端 |
| 用法 | `sshd [port] [user] [pass]` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
sshd
[sshd] starting on port 22 (one session, then exit; Ctrl+C cancels wait)
[sshd] host key generation running in background (sshd-hkgen); Ctrl+C stops WAITING only
[sshd] generating per-installation host key (RSA-2048, one-time)...
```

(备注: long-running service (the SSH daemon); the transcript shows the startup output, then the session was reset)

### tcpcc_test

| 字段 | 值 |
|---|---|
| 名称 | tcpcc_test |
| 分类 | 网络 |
| 功能 | TCP 拥塞控制测试 |
| 用法 | `tcpcc_test` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
tcpcc_test
CUBIC (RFC 8312) kernel self-test:
  cbrt(0): got=0  PASS
  cbrt(8): got=2  PASS
  cbrt(1000): got=10  PASS
  cbrt(27000): got=30  PASS
  cbrt(2^31-1): got=1290  PASS
  beta decrease: PASS
  cubic t=1s (want 96..99 MSS): got=96  PASS
  cubic t=10s (want ~1055 MSS): got=1044  PASS
  rto: ssthresh=0.7*cwnd, w_max=cwnd: PASS
  fast recovery enter: PASS
tcpcc_test: ALL PASS
    [sim] after dup ACK: fr=0 dups=1 rtx_len=1608 rtx_re=0
    [sim] after dup ACK: fr=0 dups=2 rtx_len=1608 rtx_re=0
    [sim] after dup ACK: fr=1 dups=3 rtx_len=1608 rtx_re=1
  fast retransmit + inflation: PASS
  recovery end + deflate + rtx drain: PASS
ush> 
```

### tcpstats

| 字段 | 值 |
|---|---|
| 名称 | tcpstats |
| 分类 | 网络 |
| 功能 | TCP 统计 |
| 用法 | `tcpstats` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
tcpstats
TCP reliability stats:
  (no active TCP connections)
ush> 
```

### tcptest

| 字段 | 值 |
|---|---|
| 名称 | tcptest |
| 分类 | 网络 |
| 功能 | TCP 回环测试 |
| 用法 | `tcptest` |
| 来源 | `ocos/services/net/src/svc_net_l1.c` |

**实际执行示例**：

```
tcptest
tcptest: ALL PASS (syn-opts=ok parser=ok synrcvd-recycle=ok sack-map=ok; live negotiation covered by HTTPS E2E)
ush> 
```
