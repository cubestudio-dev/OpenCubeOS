<!-- SPDX-License-Identifier: Apache-2.0 -->
# 存储故障排查

## 有序故障分类

```
oc> lspci / usb          (controller present?)
oc> ata / ahci / nvme    (family probe output - the message names it)
oc> dskstat              (block instances registered?)
oc> lsblk                (partitions scanned?)
oc> fatstat <dev>        (BPB parse of a suspect FAT volume)
oc> fsck <dev>           (filesystem check)
oc> fatmount <dev> /mnt  (mount)
oc> fstest               (the VFS/fs assertion suite)
```

## 经典问题

| 症状 | 可能原因 |
|---------|--------------|
| 有效的 FAT32 卷被拒绝（'invalid BPB'） | 错误的设备节点，或破坏对齐的堆回归（已修复；如果您修改了 mem_heap.c，请重新运行 `ata_dma_test`） |
| 重启后写入丢失 | 它存在于 ramfs 上 - 只有 /etc 和已挂载的卷会持久化 |
| `usda` 出现然后消失 | USB MSC 断开路径 - `usb_hotplug_test` 可复现 |
| ext2 卷的 ext4 挂载 | 受支持（ext2 是子集）；先检查 `fsck` |
| mkfs 拒绝执行 | 字面意义上的 `confirm` 参数是设计所必需的 |
