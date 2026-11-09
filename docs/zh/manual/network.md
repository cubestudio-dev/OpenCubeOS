<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络

一个自包含的 TCP/IP 协议栈，从网卡驱动到 SSH 服务器一应俱全——没有移植的 libc，也没有第三方代码。

## 点亮

```
oc> dhcp                  (request an address on nic0)
oc> ping 10.0.2.2         (ICMP echo to the QEMU gateway)
oc> dns example.com       (resolve via the QEMU DNS forwarder)
oc> wget 10.0.2.2         (fetch http://10.0.2.2/)
oc> wget host 8080 /path  (explicit port and path)
oc> route                 (routing table)
oc> tcpstats              (TCP counters + congestion state)
oc> firewall              (stateful firewall rules)
```

在 QEMU 中，默认的 `-device e1000` 能直接匹配 e1000 驱动：

```
qemu-system-x86_64 -m 512 -cdrom opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1
```

## 支持的网卡

| 系列 | 卡片 | 驱动 |
|--------|-------|--------|
| Intel e1000 | e1000 (8254x) | `drivers/nic/driver_nic.c` |
| Intel e1000e | 8257x ICH/PCH 网卡 | `driver_nic_e1000e.c` |
| Intel igb | 82575/82576（含虚拟功能） | `driver_nic_igb.c` |
| Intel ixgbe | 82598/82599 10 千兆以太网 | `driver_nic_ixgbe.c` |
| Realtek rtl8139 | 8139/810x 快速以太网 | `driver_nic_rtl8139.c` |
| Realtek rtl8169 | 8168/8125/810x 千兆系列 | `driver_nic_rtl8169.c` |
| Broadcom BCM57xx | 5708/5709/5714 时代 | `driver_nic_bcm57xx.c` |

## 协议栈

ARP、IPv4、ICMP、UDP、DNS、拥塞控制 TCP（`net_tcp_cc.c`）、面向用户程序的套接字 API，以及有状态防火墙。每一层都可通过命令查看：`arp`、`route`、`tcpstats`、`firewall`、`nicstat`。

## HTTPS 客户端（TLS 1.2 / 1.3）

`wget https://...` 通过 RSA 和 ECDHE 密钥交换协商 TLS，验证 X.509 链（RFC 6125 主机名策略——IP 字面 URL 无参考身份），并拒绝篡改的证书。加密核心（AES、SHA-2 家族、HMAC、RSA、NIST 曲线、Ed25519 风格原语）与 `cryptotest` 用 NIST 向量验证的代码相同。

DH 密钥交换正确性与时序：`dhtest`。

## SSH：客户端与服务器

- **客户端**（`ssh` 命令）：连接、认证、运行远程命令——通过 curve25519 风格原语进行密钥交换。
- **服务器**（`sshd`）：一个真正在客户机内部的 SSH 服务器。主机和客户端密钥存储在 /etc 卷上（`host_keygen_etc` 为其生成）；授权密钥也在那里管理。从你的主机：

  ```
  ssh -p <port> root@localhost     (forward a QEMU port to try it)
  ```

  端到端测试套件对客户机内的 sshd 执行完整的 paramiko 登录往返测试。

## DNS

`dns <name>` 通过配置的解析器解析（QEMU 的用户模式栈会转发到主机）。缓存的条目可通过网络统计命令查看。

## 缺失功能（诚实清单）

- 未实现 IPv6。
- Wi-Fi 关联：无线栈（`wifi` 命令）覆盖 ath/brcm/iwlwifi/rtlwifi 系列和 SDIO 总线上的状态/扫描/测试——真实硬件上的完整关联取决于网卡系列；在 QEMU 中没有可附加的 Wi-Fi 设备模型。
- NAT 穿越/端口转发是 QEMU 用户模式的特性，而非内核功能。
