<!-- SPDX-License-Identifier: Apache-2.0 -->
# 驱动模型

## 注册接口面

每个家族都暴露了一个 L1 注册 API：

- `driver_block_register` + 读/写/刷新 (+ set_ops) - 块设备，每个实例有名称（ata0、usda、nvme0...）
- `driver_nic_register` + 发送/接收/链路状态/获取 MAC
- `driver_snd_register` + 播放/停止/设置采样率/设置音量/获取能力
- PCI：`driver_pci_find_class_exact/mask` 用于设备发现

驱动程序探测其总线（PCI ID、USB 描述符、i2c 地址），声明设备，向其家族注册，从那时起，家族级命令和 VFS/套接字层就能统一地看到它。

## PCI 核心

`drivers/pci/driver_pci.c` + pcie：配置空间访问、BAR 解码（`pcibars`）、能力；MSI（当设备使用时）。`lspci`、`pcidump`、`pcie` 从 Shell 中检查。

## USB 核心

`drivers/usb/driver_usb.c` 协调四个主机控制器堆栈（UHCI/OHCI/EHCI/xHCI），包括设备枚举、最多到 USB_ENUM_DEPTH 层的集线器，以及上层的类驱动：MSC（大容量存储）、HID（键盘/鼠标）、串行、音频。DMA 规范：xHCI 在探测和每次传输时拒绝超过 4GiB 的地址。

## 错误路径规范

审计历史（BUG-0136..0238）让一件事变得不容协商：对于说谎的设备（截断的表、持续 NAK、垃圾响应），必须采用有界且无泄漏的回退机制。每个探测都有对应的清理路径；每次传输都会验证其边界。各家族的点亮手册：[../drivers/](../drivers/)。
