<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络问题排查

## 有序排查

运行 `net-tool` 走一遍菜单，或使用经典拼写：

```
ush> hwinfo                 (wizard: PCI page - is the NIC on the bus?)
ush> ip                     (address bound? 0.0.0.0 = none)
ush> dhcp                   (address + gateway + DNS in one shot)
ush> ping 10.0.2.2          (ICMP to the QEMU gateway)
ush> arp                    (L2 resolution happened?)
ush> dns example.com        (UDP + resolver OK?)
ush> wget example.com 80 /  (full TCP + HTTP round trip)
ush> tcpstats               (per-connection counters)
```

每一步隔离一层：总线 -> 驱动 -> L2 -> L3 -> L4 ->
应用。`update` 向导在真正检查更新之前，跑的正是这条阶梯的
前几步（IP -> DHCP 修复 -> DNS）。

## QEMU 细节

- 用户态网络永远提供 10.0.2.2（网关）和 DNS 转发——无需
  宿主机配置。
- 默认的 `-device e1000` 是测试最充分的路径。
- 双实例测试：共享的 `-netdev socket/multicast` 为网卡测试
  提供一条真实的双节点链路。
- **首次连接 sshd 停摆**：DHCP 之后 ARP 缓存是冷的——先跑
  一个客户端方向的命令（`ping` 对端）把它暖起来。

## TLS/SSH 细节

- 证书失败会打印确切的验证阶段（链/主机名/策略）——读那条
  消息；信任模型（无锚点告警、命名主机失配致命）见
  [arch/network-stack.md](../arch/network-stack.md)。
- sshd 主机密钥：首次启动时生成，或预先在 /etc 卷上播种
  `ssh_host_key`（`tools/host_keygen_etc.py`，
  或 [../dev/e2e-suites.md](../dev/e2e-suites.md) 中的 mtools
  配方）——这是 E2E 套件的前置条件。
