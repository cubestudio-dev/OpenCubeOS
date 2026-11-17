<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Network tools (4)

### ifconfig

| Field | Value |
|---|---|
| Name | ifconfig |
| Category | Network tools |
| Function | network interface status |
| Return codes | 0 |

**Executed example**:

```
i2cget
usage: i2cget <bus> <addr> [reg]  (addr/reg in hex, 0x..)
ush> 
```

### netstat

| Field | Value |
|---|---|
| Name | netstat |
| Category | Network tools |
| Function | socket/connection statistics |
| Return codes | 0 |

**Executed example**:

```
netstat
Network statistics:
  TX: 0 packets, 0 bytes
  RX: 0 packets, 0 bytes
  ARP requests: 0  replies: 0
  ICMP echo sent: 0  recv: 0
  TCP connections: 0
  UDP rx dropped (buffer full): 0
  IP frag reassembled: 0
  IP frag dropped: 0  evicted: 0
Sockets:
  (none)
ARP cache:
  (empty)
ush> 
```

### ping

| Field | Value |
|---|---|
| Name | ping |
| Category | Network tools |
| Function | ICMP echo to a host |
| Usage | `ping <host>` |
| Parameters | `host` |
| Return codes | 0 |

**Executed example**:

```
ping
usage: ping <host>
ush> 
```

### wget

| Field | Value |
|---|---|
| Name | wget |
| Category | Network tools |
| Function | download a file over HTTP(S) |
| Usage | `wget <host> [port] [path]` |
| Parameters | `host` |
| Return codes | 0 |

**Executed example**:

```
wget
usage: wget <host> [port] [path]
ush> 
```
