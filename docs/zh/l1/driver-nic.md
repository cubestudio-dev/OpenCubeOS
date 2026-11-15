<!-- SPDX-License-Identifier: Apache-2.0 -->
# 网卡与无线驱动

一个网络家族如何加入 net 服务。

## 模型（3.0.0）

- 驱动住在 `ocos/drivers/net/` - 每个家族一个文件
  （`drv_net_e1000e.c`、`drv_net_igb.c`、`drv_net_ixgbe.c`、
  `drv_net_rtl8139.c`、`drv_net_rtl8169.c`、`drv_net_bcm57xx.c`、
  `drv_net_other.c`），外加无线栈
  （`drv_net_wireless.c`、`drv_net_ath.c`、`drv_net_brcm.c`、
  `drv_net_iwlwifi.c`、`drv_net_rtlwifi.c`）。
- 身份：`OC_DRIVER_DECL("nic", "e1000e", &ops, "Intel e1000e")`
  - 802.11 家族则用 `"wireless"` 类。启动收集器探测；家族自报
  诚实状态（registered / scanning / connected）。
- 驱动内部表 API 是 `drv_net_api.h` - 内核内部，只从家族自己的
  探测路径修改。
- net 服务（`ocos/services/net/`）拥有协议栈：IPv4 + TCP/UDP、
  DHCP、DNS、防火墙/netfilter、TLS、SSH/sshd，以及 L1SVC 类别
  3/8 表面。

## 无线层

`wifi`（status / scan / test / sdio）报告每家族状态与来自
beacon 的扫描列表；`bt`（status / usb / uart / test）代理蓝牙
传输（`drv_misc_bt_usb.c` 里的 HCI-USB、H4 UART、完整的
L2CAP/SDP/RFCOMM/SPP 链）。没有硬件 -> 诚实的
"no wireless devices registered" 行，绝不是假列表。

## 一个新家族长什么样

1. `ocos/drivers/net/drv_net_<fam>.{c,h}` - PCI 探测、MAC 设置、
   把 RX/TX 接入服务的包路径。
2. `OC_DRIVER_DECL("nic", "<fam>", &ops, "description")`。
3. 可选的状态/测试操作：在 `svc_sysapi_l1svc.h` 追加编号、重新
   生成镜像、加经典表行（`net-tool` 的 `g_ops[]`）或一个薄包装
   文件夹。

## 消费者契约

应用调用服务，从不调用驱动：`net-tool ip` ->
`sys_l1svc(NET, IP, ...)`；`ping`/`wget` 走 `SYS_NETCMD` 桥
（op 2 / 4）。输出行（`"IP: ..."`、`"DHCP success: IP=..."`、
`"DNS resolution failed (timeout)"`）是更新向导和文档依赖的
锚点 - 见 [surface-map.md](surface-map.md)。
