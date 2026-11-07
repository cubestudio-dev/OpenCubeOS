<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络故障排查

## 有序排查

```
oc> lspci              (NIC on the bus?)
oc> ifconfig           (driver bound? MAC present?)
oc> nicstat            (counters moving?)
oc> dhcp               (address + gateway + DNS)
oc> ping 10.0.2.2      (ICMP to the QEMU gateway)
oc> arp                (L2 resolution happened?)
oc> dns example.com    (UDP + resolver OK?)
oc> wget 10.0.2.2      (full TCP + HTTP round trip)
```

每个步骤隔离一个层级：总线 -> 驱动 -> L2 -> L3 -> L4 -> 应用程序。

## QEMU 特定事项

- 用户态网络总是提供 10.0.2.2（网关）和 DNS 转发 - 无需主机配置。
- 默认的 `-device e1000` 是经过充分测试的最佳路径。
- 双实例测试：共享 `-netdev socket/multicast` 为网卡测试提供真实的双节点链路。

## TLS/SSH 特定事项

- 证书验证失败会打印确切的验证阶段（链/主机名/策略）- 请阅读消息内容；信任模型（非锚定警告、命名主机不匹配是致命的）详见 [arch/network-stack.md](../arch/network-stack.md)。
- sshd 需要 /etc 卷已初始化（`host_keygen_etc`）- 这是 E2E 套件的前置条件。
