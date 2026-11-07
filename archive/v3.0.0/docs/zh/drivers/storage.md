<!-- SPDX-License-Identifier: Apache-2.0 -->
# 存储点亮手册

## 家族与命令

| 驱动 | QEMU 挂载 | 命令 | 测试 |
|--------|-------------|---------|------|
| ATA/ATAPI PIO+DMA | 默认 IDE | `ata` | `ata_dma_test`, `disk_rw_test` |
| AHCI | `-machine q35` (ich9-ahci) | `ahci` | `ahci_test` |
| NVMe | `-device nvme` | `nvme` | `nvme_test` |
| virtio-blk | `-device virtio-blk-pci` | `lsblk`/`dskstat` | `virtio_blk_test` |
| USB MSC | `-device usb-storage` (xHCI) | `usb`, `usbdev` | `usb_storage_test` |
| SD/SDHCI | 板卡特定 | `lsblk` | (家族驱动) |
| SCSI LSI | `-device lsi53c810` | `scsi` | `scsitest` |

## 点亮顺序

1. `lspci` / `usb` - 设备在总线上。
2. 家族探测程序声明该设备并注册一个块设备实例
   (`ata0`, `nvme0`, `usda`...) - `dskstat`。
3. 分区扫描暴露分区；`lsblk` 显示树状结构。
4. 文件系统：`mkfs.fat32 <dev> confirm` + `fatmount <dev> /mnt`，或
   直接挂载现有卷。
5. 门禁：`fstest` + `disk_rw_test` + 一次挂载/卸载周期。

## 已知风险点

- ATA-DMA PRDT 对齐依赖于堆的 32 字节头部 -
  如果你修改了 mem_heap.c，请重新运行 `ata_dma_test` + 一次 FAT32 挂载。
- xHCI/任何 32 位 DMA 设备：高于 4GiB 的地址在设计上会在
  探测和每次传输时被拒绝。
- virtio 首次 I/O 前需要协商特性；驱动程序处理版本协商
  （传统/现代）。
