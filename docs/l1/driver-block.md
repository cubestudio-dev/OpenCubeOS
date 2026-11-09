<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: block drivers

Register a storage device so the whole system (VFS, partitions, fs
utilities) sees it.

## The surface

`drivers/block/driver_block_blk.h` (17 declarations; the numbered
8):

```
driver_block_register(name, ops, ctx)     -> device id
driver_block_read(dev, lba, count, buf)   -> bytes read / <0
driver_block_write(dev, lba, count, buf)  -> bytes written / <0
driver_block_flush(dev)                   -> 0 / <0
driver_block_set_ops(dev, ops)
```

Plus the family init entries that bind a discovered PCI device to a
driver: `driver_block_ahci_init(pci)`, `driver_block_nvme_init(pci)`,
`driver_block_ata_dma_init(pci)`.

## Implementing a driver

1. Probe your bus (PCI class match via
   `driver_pci_find_class_mask`).
2. Map BARs, reset the device, negotiate features.
3. Build an ops struct (read/write/flush in sector units) and
   `driver_block_register("myd0", &ops, ctx)` - the per-instance name
   is what `dskstat` shows.
4. Fail cleanly: refusal at probe with a message beats a silent
   half-registration.

## Rules with teeth

- DMA bounds: 32-bit-mask devices must reject above-4GiB addresses
  at probe AND per transfer (the xHCI precedent).
- Alignment: ATA-DMA PRDT alignment rides on the heap's 32-byte
  header guarantee.
- The tests that gate you: `fstest`, `disk_rw_test`, the per-driver
  `*_test` commands.
