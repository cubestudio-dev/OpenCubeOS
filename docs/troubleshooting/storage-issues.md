<!-- SPDX-License-Identifier: Apache-2.0 -->
# Storage troubleshooting

## Ordered triage

Run `disk-tool` and walk the menu, or use the classic spellings:

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

## The classics

| Symptom | Likely cause |
|---------|--------------|
| valid FAT32 volume refused ('invalid BPB') | wrong device node, or the alignment-destroying heap regression (fixed; rerun `ata_dma_test` if you touched mem_heap.c) |
| write lost after reboot | it lived on ramfs - only /etc and mounted volumes persist |
| USB stick appears then vanishes | USB MSC detach path - `usb_hotplug_test` reproduces |
| ext4 mount of an ext2 volume | supported (ext2 is a subset); check `fsck` first |
| wizard asks twice for the device name | by design - mkfs/abdisk/install paths require the retype + bare-y confirmation |
| A/B disk "absent" right after abdisk | re-run `update status`; the wizard re-checks in a loop until the service sees it |

## The destructive paths

`mkfs.*`, `abdisk`, `install` destroy data. In the wizards they are
guarded (retype the device name, then a bare `y`); through
classic/direct dispatch they run immediately - exactly like the
historical commands - so scripts keep their semantics. Double-check
the device name against `lsblk` before the classic forms.
