<!-- SPDX-License-Identifier: Apache-2.0 -->
# Storage bring-up manual

## Families and commands

| Driver | QEMU attach | Command | Test |
|--------|-------------|---------|------|
| ATA/ATAPI PIO+DMA | default IDE | `ata` | `ata_dma_test`, `disk_rw_test` |
| AHCI | `-machine q35` (ich9-ahci) | `ahci` | `ahci_test` |
| NVMe | `-device nvme` | `nvme` | `nvme_test` |
| virtio-blk | `-device virtio-blk-pci` | `lsblk`/`dskstat` | `virtio_blk_test` |
| USB MSC | `-device usb-storage` (xHCI) | `usb`, `usbdev` | `usb_storage_test` |
| SD/SDHCI | board-specific | `lsblk` | (family driver) |
| SCSI LSI | `-device lsi53c810` | `scsi` | `scsitest` |

## Bring-up order

1. `lspci` / `usb` - the device is on the bus.
2. The family probe claims it and registers a block instance
   (`ata0`, `nvme0`, `usda`...) - `dskstat`.
3. Partition scan exposes partitions; `lsblk` shows the tree.
4. Filesystem: `mkfs.fat32 <dev> confirm` + `fatmount <dev> /mnt`, or
   mount an existing volume directly.
5. Gate: `fstest` + `disk_rw_test` + a mount/unmount cycle.

## Known sharp edges

- ATA-DMA PRDT alignment depends on the heap's 32-byte header -
  if you touch mem_heap.c, rerun `ata_dma_test` + a FAT32 mount.
- xHCI/any 32-bit-DMA device: above-4GiB addresses are refused at
  probe and per-transfer by design.
- virtio requires the negotiated features before first I/O; the
  driver handles version negotiation (legacy/modern).
