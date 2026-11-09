<!-- SPDX-License-Identifier: Apache-2.0 -->
# PCI / PCI Express

## 核心

`drivers/pci/driver_pci.c` + `driver_pcie.c`：配置空间访问
（类型 0/1）、BAR 解码、桥接处理、功能、以及设备使用 MSI 的情况。

## L1 层发现

`driver_pci_find_class_exact` / `driver_pci_find_class_mask` - 各个
家族初始化所使用的编号接口面（AHCI/NVMe/NIC/声卡初始化会接收 pci_dev）。

## 命令

```
oc> lspci           (device list)
oc> pcidump         (full config-space dump)
oc> pcibars         (BAR decode)
oc> pcie            (PCIe capabilities/links)
oc> msitest         (MSI paths)
```

## 点亮笔记

- QEMU q35 暴露的 PCIe 拓扑比默认的 i440fx 更丰富 - 两者都受支持；
  热插拔端口通过常规的探测路径工作。
- 64 位 BAR：高位字读取会被验证（一个 xHCI 探测拒绝路径恰好测试了这一点）。
