<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：网卡驱动

## 接口面

`drivers/nic/driver_nic.h`（34 个声明；其中编号的 13 个）：

```
driver_nic_register(name, ops, ctx)
driver_nic_send(dev, frame, len)
driver_nic_recv(dev, buf, maxlen)        -> length / 0 / <0
driver_nic_link_status(dev)              -> link up/down
driver_nic_get_mac(dev, out[6])
```

系列初始化函数：`driver_nic_e1000e_init`、`_igb_init`、`_ixgbe_init`、
`_rtl8139_init`、`rtl8168_init`、`rtl8125_init`、`rtl810x_init`、
`bcm57xx_init`。

## 协议栈期望

帧为原始以太网帧；协议栈（`net/net_core.c`）拥有 ARP 及以上的协议。RX 可根据你注册的操作选择轮询或中断驱动方式。`ifconfig`、`nicstat`、`*_test` 命令会观察这些操作。

## 参考实现

在编写自己的驱动前，请阅读一个 Intel（e1000e）和一个 Realtek（rtl8169 系列）的驱动：描述符环、链路状态处理以及错误路径（总线主控禁用、超时重置）的布局方式不同，但契约是相同的。
