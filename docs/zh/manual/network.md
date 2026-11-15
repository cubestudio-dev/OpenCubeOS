<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络

网络操作面位于 `net-tool`（向导）及其之下的经典命令背后。

## 地址配置

```
ush> net-tool               -> "obtain an address via DHCP"
ush> dhcp                   (the classic form - same call)
ush> ip                     (show IP / mask / gateway)
```

通过向导做静态配置时，地址、掩码和网关逐字段采集，因此位置参数行永远不会以错误的顺序拼接。经典形式：

```
ush> ip 10.0.2.15 255.255.255.0 10.0.2.2
```

## 域名解析

```
ush> dns example.com              A record (+CNAME when present)
ush> dns example.com aaaa         AAAA / cname / mx / txt / ns / srv
ush> dnstest                      every record type against the resolver
```

DNS 经过所配置的解析器；在 QEMU 用户态网络下，slirp 协议栈会把请求转发给真实的上游。

## 连通性测试

```
ush> net-tool               -> ping / wget / ssh / dnstest submenu
ush> ping 10.0.2.2
ush> wget example.com 80 /
ush> wget https.example 443 /           (TLS is built in)
```

`ping` 和 `wget` 走内核的 NETCMD 桥——正是历史应用所用的那个系统调用。

## WiFi 与蓝牙

```
ush> net-tool               -> WiFi manager: status / scan / test / sdio
ush> wifi scan              (beacon-derived network list)
ush> net-tool               -> Bluetooth: status / usb / uart / test
ush> bt                     (transport + controller status)
```

无线层按家族（ath、brcm、iwlwifi、rtlwifi）如实报告状态：已注册、扫描中、已连接——没有相应硬件的机器会直说，而不是假装。

## 防火墙

```
ush> firewall               (list rules + policies)
ush> firewall add ...       (rule management - see the commands page)
ush> firewall ct            (the connection-tracking table)
```

## 路由与 ARP

```
ush> route                  (the full routing table)
ush> route add 10.0.0.0 255.0.0.0 10.0.2.2
ush> arp                    (the ARP cache)
```

## SSH 进出

```
ush> sshd                   (serve the shell - port/user/pass optional)
ush> ssh 10.0.2.2           (connect out; credentials as arguments)
```

SSH 协议栈运行真实的 KEX/加密协商流程；主机密钥可预置在 /etc 卷上以支持无人值守启动。在 QEMU 下，用 `-netdev user,hostfwd=tcp::2223-:22` 转发端口，再从宿主机连接。

## 故障排查

- **DHCP 之后没有 IP**——检查网卡型号（`hwinfo` -> PCI）以及 `-netdev user` 是否存在；e1000 系列测试最充分。
- **DNS 超时**——slirp 的 DNS 转发只在用户态网络下有效；在桥接 tap 上需要真实的解析器。
- **首次 SSH 连接卡住**——DHCP 之后 ARP 缓存还是冷的；先跑一条客户端方向的命令（`ping` 对端）预热，再连接。

更多内容见 [../troubleshooting/network-issues.md](../troubleshooting/network-issues.md)。
