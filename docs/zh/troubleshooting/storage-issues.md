<!-- SPDX-License-Identifier: Apache-2.0 -->
# 存储问题排查

## 有序排查

运行 `disk-tool` 走一遍菜单，或使用经典拼写：

```
ush> hwinfo                 (wizard: PCI/USB pages - controller present?)
ush> ata / ahci / nvme      (family probe output - the message names it)
ush> dskstat                (block instances registered?)
ush> lsblk                  (devices + partitions scanned?)
ush> parted <dev>           (the partition table)
ush> fatstat                (FAT statistics of a suspect volume)
ush> fsck <dev>             (filesystem check)
ush> fatmount <dev> /mnt    (mount)
ush> fstest                 (the VFS/fs assertion suite)
```

## 经典问题

| 症状 | 可能原因 |
|---------|--------------|
| 有效的 FAT32 卷被拒绝（'invalid BPB'） | 设备节点不对，或破坏对齐的堆回归（已修复；若动过 mem_heap.c 请重跑 `ata_dma_test`） |
| 重启后写入丢失 | 它住在 ramfs 上——只有 /etc 和已挂载的卷持久 |
| U 盘出现后又消失 | USB MSC 拔出路径——`usb_hotplug_test` 可复现 |
| 以 ext4 挂载 ext2 卷 | 受支持（ext2 是子集）；先查 `fsck` |
| 向导两次询问设备名 | 设计如此——mkfs/abdisk/install 路径要求重输 + 裸 y 确认 |
| abdisk 之后 A/B 磁盘"缺席" | 重跑 `update status`；向导会循环复查，直到服务看见它 |

## 破坏性路径

`mkfs.*`、`abdisk`、`install` 会毁数据。在向导中它们有防护
（重输设备名，再一个裸 `y`）；经经典/direct 分发则立即执行
——与历史命令完全一致——脚本因此保住语义。使用经典形式
之前，对照 `lsblk` 把设备名再核对一遍。
