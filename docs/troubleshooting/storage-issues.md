<!-- SPDX-License-Identifier: Apache-2.0 -->
# Storage troubleshooting

## Ordered triage

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

## The classics

| Symptom | Likely cause |
|---------|--------------|
| valid FAT32 volume refused ('invalid BPB') | wrong device node, or the alignment-destroying heap regression (fixed; rerun `ata_dma_test` if you touched mem_heap.c) |
| write lost after reboot | it lived on ramfs - only /etc and mounted volumes persist |
| `usda` appears then vanishes | USB MSC detach path - `usb_hotplug_test` reproduces |
| ext4 mount of an ext2 volume | supported (ext2 is a subset); check `fsck` first |
| mkfs refuses | the literal `confirm` argument is required by design |
