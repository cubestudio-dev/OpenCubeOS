<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：块设备驱动

注册一个存储设备，使整个系统（VFS、分区、文件系统工具）都能识别它。

## 接口面

`drivers/block/driver_block_blk.h`（17 个声明；编号为 8 的）：

```
driver_block_register(name, ops, ctx)     -> device id
driver_block_read(dev, lba, count, buf)   -> bytes read / <0
driver_block_write(dev, lba, count, buf)  -> bytes written / <0
driver_block_flush(dev)                   -> 0 / <0
driver_block_set_ops(dev, ops)
```

加上将发现的 PCI 设备绑定到驱动的家族初始化条目：`driver_block_ahci_init(pci)`、`driver_block_nvme_init(pci)`、`driver_block_ata_dma_init(pci)`。

## 实现驱动程序

1. 探测你的总线（通过 `driver_pci_find_class_mask` 进行 PCI 类别匹配）。
2. 映射 BAR，重置设备，协商特性。
3. 构建一个操作结构体（以扇区为单位的读写/刷新）并调用 `driver_block_register("myd0", &ops, ctx)` - 实例名称是 `dskstat` 显示的内容。
4. 优雅地失败：探测时拒绝并附带消息，胜过静默的部分注册。

## 带有约束的规则

- DMA 边界：32 位掩码设备必须在探测时和每次传输时拒绝超过 4GiB 的地址（遵循 xHCI 先例）。
- 对齐：ATA-DMA PRDT 对齐依赖于堆的 32 字节头保证。
- 作为门禁的测试：`fstest`、`disk_rw_test`、各驱动的 `*_test` 命令。
