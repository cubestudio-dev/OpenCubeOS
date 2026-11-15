<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网络排查

先按序排查，然后是四条深链：DHCP、DNS、TLS
自签名拒绝、以及逐 NIC 的 `init failed` 模板。

## 按序排查

运行 `net-tool` 走菜单，或使用经典拼写：

```
ush> lspci                  (is the NIC on the bus?)
ush> ip                     (address bound? 0.0.0.0 = none)
ush> dhcp                   (address + gateway + DNS in one shot)
ush> ping 10.0.2.2          (ICMP to the QEMU gateway)
ush> arp                    (L2 resolution happened?)
ush> dns example.com        (UDP + resolver OK?)
ush> wget example.com 80 /  (full TCP + HTTP round trip)
ush> tcpstats               (per-connection counters)
```

每步隔离一层：总线 → 驱动 → L2 → L3 → L4 →
应用。`update` 向导在检查更新之前运行的正是这条
阶梯的前几步（IP 探测 → DHCP 提议 → DNS）。
`netstat` 与 `ifconfig` 给出套接字/接口图景。

## DHCP 失败链

DHCP 是一次 4 步交换 — 端口 68/67 上的 DISCOVER → OFFER → REQUEST → ACK，
魔数 0x63825363，xid 0x12345678；解析的选项：53
（消息类型）、54（服务器 id，在 REQUEST 中回显）、1（掩码）、3（网关）、
6（DNS）。唯一的失败串是 `DHCP failed (timeout)` —
链路在某处断开，超时吞掉了细节。走一遍：

1. **到底有没有 NIC？** `lspci`；驱动的状态命令
   （`e1000`、`rtl8139`、...）。`ip` 显示绑定的驱动名。
2. **驱动绑定了吗？** 见下文的 `<nic>: init failed` 模板。
3. **L2 活着吗？** `arp` — 任何流量尝试之后应当有
   网关的 MAC。空空如也 = 没有帧在流动。
4. **服务器应答吗？** QEMU 用户态网络恒有应答
   （10.0.2.2 网关 + DNS 转发，无需宿主配置）。
   真实 LAN：检查 DHCP 服务器的日志。
5. **重跑** `dhcp` — 成功打印锚点 `DHCP success: IP=...` 外加掩码/网关/DNS。退出码反映结局。

如果 DHCP 成功但 DNS 字段为空，用静态 `ip` 设置它
或重跑 DHCP（选项 6 的解析是链路的一部分）。

## DNS 失败链

`dns <name>`（可选 `dns <name> aaaa|cname|mx|txt|ns|srv`）。
失败串：`DNS resolution failed`、`DNS resolution failed (timeout)`、`timeout`，以及逐记录的如实尾巴（`MX: no mail exchanger (or timeout)`、`AAAA: no IPv6 record (or timeout)`、
`CNAME: not a CNAME or resolution failed`、...）。

1. **配置了 DNS 服务器吗？** 它来自 DHCP 选项 6
   或一轮静态 `ip` — `ifconfig` 打印 DNS 字段。
2. **UDP 在流动吗？** `ping <dns-server-ip>`；对网关 `arp`。
3. **解析器到底能工作吗？** `dnstest`（测试组，含针对
   10.0.2.2 与 update.json 固定件的内建向量）。
4. **投毒防护**（klog）：`wrong transaction id dropped`、
   `query (QR=0) dropped`、`wrong qname echo dropped` — 未通过
   校验的应答被静默丢弃。在敌意 LAN 上，已知
   边界：事务 ID 与应答校验加固属于
   BUG-22 类。
5. netstat 家族输出中的计数器 `UDP rx dropped (buffer full)` /
   `IP frag dropped` 意味着过载，不是配置错误。

## TLS 与自签名拒绝（HTTPS E2E 第 0 项）

信任模型：证书链必须锚定于内嵌根存储
（ISRG Root X1 等，DER 由 `tools/embed_roots.py` 生成）。
校验失败打印确切的阶段然后拒绝：

```
[tls] x509 verify failed: <why> - connection REFUSED
```

其中 `<why>` 是 `bad certificate format`、`unsupported algorithm`、`signature verification failed`、`hostname mismatch`、
`issuer certificate missing` 之一（X509_E_* 家族 —
[error-codes.md](error-codes.md)）。

**HTTPS E2E 第 0 项的语义**：HTTPS E2E
套件的*第零*项检查就是**自签名链默认被拒绝**。这是
BUG-21 的验收测试（旧代码只警告并
继续 — MITM 只需一张自签名证书）。因此：

- 在自签名服务器上看到 REFUSED 行是**正确
  行为**，不是故障。
- 针对自签名参考服务器测试需要显式
  选择加入：`config set tls.insecure 1`（E2E 兼容轮用的
  正是它），事后改回。
- 校验之外的握手步骤失败（`ServerHello recv/parse failed`、`ServerHello legacy version mismatch`、
  `CertificateVerify recv failed`、`server Finished verify_data mismatch`、`record decrypt failed (AEAD open)`）直接指名损坏
  步骤 — 读消息；TLS 1.2 的 DHE-RSA-AES128-SHA256
  （0x0067）是参考协商。
- `wget https://host/path` 是客户端界面（`https://` 选择 TLS，
  默认端口 443）。

## 逐 NIC 的 `init failed` 模板

每个以太网驱动探针失败时打印相同的失败形状：
`<name>: init failed` — `e1000e`、`igb`、`ixgbe`、`rtl8139`、
`rtl8168`（rtl8169 界面）、`bcm57xx`（外加其特殊的
`bcm57xx: DMA memory above 4GiB - refused`）。统一排查：

```
ush> lspci                       # 1. is the card on the bus at all?
ush> pcidump 0:03.0              # 2. are the BARs sane? (use the real b:d.f)
ush> e1000e                      # 3. the family status command
ush> e1000e_test                 # 4. the family selftest
ush> nic_rw_test                 # 5. generic NIC loopback
ush> other_nic_test              # 6. best-effort families (3c59x/nForce/AR81xx/Marvell)
```

- 无 PCI 设备 → QEMU flag 错误或卡未插好；QEMU
  测试最充分的路径是 user netdev 上的 `-device e1000`。
- 设备在场但 `init failed` → BAR/DMA 问题
  （`pcibars`、`pcie`）；对 bcm57xx 而言，4 GiB 以上 DMA 被明确
  拒绝。
- 状态 OK 但无流量 → 链路/路由：`ifconfig`（链路状态）、
  `route`、`arp`。
- `virtio TX timeout: avail_idx=... used_idx=...` — virtio NIC
  停止消费描述符；重启 VM/设备，若持续则
  报告。

无线：`wifi`（状态）、`wifi scan`（被动扫描 — QEMU 如实
报告无射频）、`wifi test`（核心 + 逐家族自测）、
`wifi sdio`。安全字符串 `WEP/WPA1/WPA2 (not supported)` 是
如实状态机，不是失败。

## SSH / sshd 细节

- 客户端默认：端口 2222，用户 `oc`，密码 `oc`；密码 `-` =
  配合 `/etc/ssh_client_key` 的公钥认证（密钥
  缺失时回退到密码）。主机名经 DNS 解析。
- 主机信任是 TOFU：首次连接写入
  `/etc/ssh_known_hosts`；密钥变化打印 `[ssh] WARNING: HOST KEY CHANGED! (possible MITM)` 并拒绝 — 仅当你知道
  变化原因时才删除该条目。在只读 /etc 上预期
  `[ssh] TOFU: cannot write /etc/ssh_known_hosts`。
- 服务器：`sshd [port]`（默认 22），同时一次一会话；第二个
  连接被拒（`too many connections (one session at a time)`）。首次启动在后台生成主机密钥
  （`host key file corrupt; regenerating`）；如果不能
  （`cannot obtain a per-installation host key - refusing to start sshd`），用 `tools/host_keygen_etc.py` 预置
  `etc.img`。
- **首次连接停滞**：DHCP 之后 ARP 缓存是冷的 — 先跑一条
  客户端方向的命令（对对端 `ping`）热身。
- TCG（无 KVM）QEMU 很慢：RSA-2048 密钥生成可能超过 500 s —
  E2E 配方预置密钥并使用宽裕超时（横幅 420 s）。

## QEMU 细节

- 用户态网络恒提供 10.0.2.2（网关）与 DNS
  转发 — 无需宿主配置。
- 默认 `-device e1000` 是测试最充分的路径。
- 双实例测试：共享 `-netdev socket,multicast=...` 为
  NIC 测试提供真实的双节点链路。
- 网络测试的时序稳定性：QEMU 的 `-icount`。

## 防火墙与路由

完整的 netfilter 界面（`firewall` 列出规则；`add`/`del`/
`policy`/`ct`/`reset` 管理它们）：

```
ush> firewall                                    # list rules + policies
ush> firewall add in drop tcp port 22            # drop inbound SSH
ush> firewall del 1                              # remove rule 1
ush> firewall policy in accept                   # chain default
ush> firewall ct                                 # conntrack table
ush> firewall reset                              # rules + conntrack + policies
```

- `rule not found` — `del <n>` 的索引过期；先重新列出。
- 试验后 ping 失效 — 检查 input-drop 规则并
  `firewall reset`（它同时清除 conntrack 与策略 — 这正是
  要点）。
- `nf_test` 是自校验测试组（input drop + 命中，规则
  移除后 ping 恢复）。

路由：`route` 显示默认项与至多 16 条自定义条目；
`route add <dst> <mask> <gw>` / `route del <dst> <mask>`。
网关在场时的 `connect failed` 通常问题在对端，不在
表 — 但会话中途过期的 ARP 项可能死等（重传压力下的
已知 BUG-24 类）：重新对对端 `ping` 以重新解析。

## 讲述故事的计数器

- `netstat` — 套接字与连接统计。
- `tcpstats` — 逐连接状态、cwnd/ssthresh/mss/rto、
  重传队列长度、重复 ACK、时间戳、cc=cubic、w_max、
  sack、srtt/rttvar。RTO 攀升 + dup_acks 增长 = 路径上
  有丢包。
- `  UDP rx dropped (buffer full): <n>` / `  IP frag dropped: <n>`
  （统计输出） — 过载计数器，不是配置错误。
- `arp` — 16 条缓存；任何流量之后应当出现
  网关的 MAC。IP 已绑定而缓存为空意味着 L2 死了
  （驱动/链路）。
- `ifconfig` — 只读：驱动名、HWaddr、inet、掩码、网关、
  DNS、链路状态、RX/TX 计数器。`ip` 显示同一地址块；
  `ip <a> <m> <g>` 是静态设置形式（向导的静态 IP
  路径正是按此顺序询问三个字段）。

## 双节点与宿主访问设置

- 宿主访问客户机 `sshd`：`-netdev user,id=n1,hostfwd=tcp::2223-:22`
  然后在客户机中 `sshd 22`；从宿主连接
  `localhost:2223`。
- 真实双节点链路：两个实例共享 `-netdev socket,multicast=...`
  （NIC 测试使用它）。
- BT 栈是经 COM2 H4 字节的真实 E2E（用 `bt test` 对阵
  `bt_test_peer.py` 宿主对端）。

## 相关页面

- [error-catalog.md](error-catalog.md) §5.15-§5.19 — 每条网络字符串
- [silent-failures.md](silent-failures.md) — 安静的拒绝、UDP 丢弃
- [../manual/network.md](../manual/network.md) — 用户指南
- [../manual/commands/wget.md](../manual/commands/wget.md)、
  [../manual/commands/ssh.md](../manual/commands/ssh.md)
