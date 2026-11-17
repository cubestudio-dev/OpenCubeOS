<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 网络工具 (4)

### ifconfig

| 字段 | 值 |
|---|---|
| 名称 | ifconfig |
| 分类 | 网络工具 |
| 功能 | 网络接口状态 |
| 返回码 | 0 |

**实际执行示例**：

```
ifconfig
no NIC detected
ush> 
```

### netstat

| 字段 | 值 |
|---|---|
| 名称 | netstat |
| 分类 | 网络工具 |
| 功能 | 套接字/连接统计 |
| 返回码 | 0 |

**实际执行示例**：

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

| 字段 | 值 |
|---|---|
| 名称 | ping |
| 分类 | 网络工具 |
| 功能 | 对主机发 ICMP 回显 |
| 用法 | `ping <host>` |
| 参数 | `host` |
| 返回码 | 0 |

**实际执行示例**：

```
ping
usage: ping <host>
ush> 
```

### wget

| 字段 | 值 |
|---|---|
| 名称 | wget |
| 分类 | 网络工具 |
| 功能 | 通过 HTTP(S) 下载文件 |
| 用法 | `wget <host> [port] [path]` |
| 参数 | `host` |
| 返回码 | 0 |

**实际执行示例**：

```
wget
usage: wget <host> [port] [path]
ush> 
```
