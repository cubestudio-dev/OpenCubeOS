<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Networking (14)

### arp

| Field | Value |
|---|---|
| Name | arp |
| Category | Networking |
| Function | ARP table |
| Usage | `arp` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
arp
ARP cache:
  (cache empty)
ush> 
```

### dhcp

| Field | Value |
|---|---|
| Name | dhcp |
| Category | Networking |
| Function | DHCP lease status/renew |
| Usage | `dhcp` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
dhcp
Sending DHCP discover...
DHCP failed (timeout)
ush> 
```

### dns

| Field | Value |
|---|---|
| Name | dns |
| Category | Networking |
| Function | resolve a hostname |
| Usage | `dns <host>` |
| Parameters | `host` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
dns
usage: dns <name> [aaaa|cname|mx|txt|ns|srv]
ush> 
```

### dnstest

| Field | Value |
|---|---|
| Name | dnstest |
| Category | Networking |
| Function | DNS resolver test |
| Usage | `dnstest` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | firewall |
| Category | Networking |
| Function | packet filter rules |
| Usage | `firewall [...]` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
firewall
Firewall rules:
  (no rules)
policies: input=ACCEPT output=ACCEPT forward=ACCEPT
conntrack: 0 flows (firewall ct for details)
ush> 
```

### ip

| Field | Value |
|---|---|
| Name | ip |
| Category | Networking |
| Function | IP address configuration |
| Usage | `ip` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
ip
IP: 0.0.0.0
Mask: 0.0.0.0
Gateway: 0.0.0.0
ush> 
```

### lspci

| Field | Value |
|---|---|
| Name | lspci |
| Category | Networking |
| Function | list PCI devices |
| Usage | `lspci` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | nf_test |
| Category | Networking |
| Function | netfilter test |
| Usage | `nf_test` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | route |
| Category | Networking |
| Function | routing table |
| Usage | `route` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
route
Routing table:
  0.0.0.0 mask=0.0.0.0 gw=0.0.0.0 (default)
  (no custom routes)
ush> 
```

### ssh

| Field | Value |
|---|---|
| Name | ssh |
| Category | Networking |
| Function | SSH client |
| Usage | `ssh <ip> [port] [user] [pass]` |
| Parameters | `ip` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
ssh
usage: ssh <ip> [port=2222] [user=oc] [password=oc|-]
  password "-" = publickey auth (needs /etc/ssh_client_key; falls back to password if absent)
ush> 
```

### sshd

| Field | Value |
|---|---|
| Name | sshd |
| Category | Networking |
| Function | SSH server |
| Usage | `sshd [port] [user] [pass]` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
sshd
[sshd] starting on port 22 (one session, then exit; Ctrl+C cancels wait)
[sshd] host key generation running in background (sshd-hkgen); Ctrl+C stops WAITING only
[sshd] generating per-installation host key (RSA-2048, one-time)...
```

(Note: long-running service (the SSH daemon); the transcript shows the startup output, then the session was reset)

### tcpcc_test

| Field | Value |
|---|---|
| Name | tcpcc_test |
| Category | Networking |
| Function | TCP congestion control test |
| Usage | `tcpcc_test` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | tcpstats |
| Category | Networking |
| Function | TCP statistics |
| Usage | `tcpstats` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
tcpstats
TCP reliability stats:
  (no active TCP connections)
ush> 
```

### tcptest

| Field | Value |
|---|---|
| Name | tcptest |
| Category | Networking |
| Function | TCP loopback test |
| Usage | `tcptest` |
| Source | `ocos/services/net/src/svc_net_l1.c` |

**Executed example**:

```
tcptest
tcptest: ALL PASS (syn-opts=ok parser=ok synrcvd-recycle=ok sack-map=ok; live negotiation covered by HTTPS E2E)
ush> 
```
