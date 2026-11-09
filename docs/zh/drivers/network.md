<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网卡点亮手册

## 系列

| 驱动 | PCI ID（系列） | QEMU | 命令 | 测试 |
|--------|------------------|------|---------|------|
| e1000 | 8086:100e 等 | `-device e1000`（默认） | `ifconfig` | 通过 dhcp 运行 `dhcptest` |
| e1000e | 8086:10d3 等 | `-device e1000e` | `e1000e` | `e1000e_test` |
| igb | 8086:1533 时代 |（直通） | `igb` | `igb_test` |
| ixgbe | 8086:10fb 时代 |（直通） | `ixgbe` | `ixgbe_test` |
| rtl8139 | 10ec:8139 | `-device rtl8139` | `rtl8139` | `rtl8139_test` |
| rtl8169 系列 | 10ec:8168/8125/810x |（真实硬件） | `rtl8168`/`rtl8125`/`rtl810x` | `*_test` |
| BCM57xx | 14e4:16xx 时代 |（真实硬件） | `bcm57xx` | `bcm57xx_test` |

## 点亮顺序

1. `lspci` 显示网卡；系列初始化程序会绑定它。
2. `ifconfig` 显示接口 + MAC；`nicstat` 显示计数器。
3. `dhcp`（或静态 `ip`/`ifconfig` 配置）。
4. `ping 10.0.2.2` 然后 `dns example.com` 然后 `wget <host>`。
5. 门禁：`nic_rw_test`、`tcptest`、SSH/HTTPS 端到端测试。

## 两个实例之间的环回

共享 socket/udp 多播上的两个 QEMU 实例为网卡测试构成了一个真实的双节点网络 - `*_test` 命令同时针对单节点和对等场景编写。

## 注意事项

- RX 校验和链：任何网卡变更都会重新运行 `tcpstats` + `tcptest`
  （双补码 Bug 正好藏在那里）。
- 链路状态轮询与中断因系列而异；`link_status` 操作是契约。
