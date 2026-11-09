<!-- SPDX-License-Identifier: Apache-2.0 -->
# 蓝牙

## 协议栈

`drivers/bluetooth/`：HCI (`driver_bt_hci.c`)、H4 传输层 (`driver_bt_h4.c`)、L2CAP、RFCOMM、SDP、SPP（串行配置文件）、HID（输入设备）以及 USB 连接的控制器 (`driver_bt_usb.c`)。

## 命令

```
oc> bt    (controller status, inquiry, services)
```

## 点亮流程

HCI 传输层初始化（H4 UART 或 USB 适配器）、控制器复位，然后建立 SPP 的 L2CAP/RFCOMM 通道。分层测试通过 `bt` 命令运行。
