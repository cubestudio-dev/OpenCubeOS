<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络协议栈

自底向上，每层对应一个文件，位于 `net/` 目录下：

## 网卡驱动 (`drivers/nic/`)

e1000、e1000e、igb、ixgbe（Intel 系列）；rtl8139 和
rtl8169 系列（8168/8125/810x）；BCM57xx。每个驱动通过
L1 driver_nic 接口（`driver_nic_register` / send / recv /
link_status / get_mac）进行注册。DMA 描述符、支持中断驱动的
RX 以及每卡特殊处理均在驱动文件中实现 - 各系列的点亮说明
位于 [../drivers/](../drivers/)。

## 以太网 + ARP

按以太网类型进行帧 RX 分发；带过期机制的 ARP 缓存；
`arp` 命令。

## IPv4 / ICMP / UDP

分片处理、ICMP 回显（`ping` 命令）、UDP 套接字 -
DNS 通过 UDP 运行，解析器通过网络配置设置。

## TCP (`net/net_core.c` + `net_tcp_cc.c`)

包含拥塞控制的完整状态机。RX 校验和链曾是一个著名缺陷
（BUG-0072：链式 internet_checksum() 调用被重复补全 -
每个入站段均被拒绝；通过 checksum_raw_add + 在伪首部与
段之间进行一次折叠修复）。`tcpstats`、`tcpcc_test`、
`tcptest` 是观察者/测试程序。

## 用户程序的套接字

套接字系统调用族（53-syscall 接口）支持 `run select_test`
风格的复用以及网络用户程序。

## TLS 1.2 / 1.3 (`net/net_tls.c`)

RSA 和 ECDHE 密钥交换，X.509 链验证（采用 RFC 6125
主机名策略，IP 字面 URL 无参考身份 - 握手中断开连接的缺陷
正好在此处，通过 net_tls_host_is_ip_literal 修复），
以及 WP-09 信任模型：结构边界始终致命；未锚定链会警告并
继续；主机名不匹配仅对用户提供的主名致命。

## SSH 客户端 + 服务器 (`net_ssh.c`, `net_sshd.c`)

密钥交换、/etc 上的主机密钥、authorized_keys、完整会话
往返 - E2E 套件使用主机上的 paramiko 登录。

## 熵

HMAC_DRBG（SP 800-90A）从 RDRAND + RDTSC 播种，每次生成
时重新混合 - 参见
[internals/drbg-crypto.md](../internals/drbg-crypto.md)。
