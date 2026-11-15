<!-- SPDX-License-Identifier: Apache-2.0 -->
# Block drivers

How a storage device family joins the block service so the whole
system (VFS, partitions, fs utilities, the disk-tool wizard) sees
it.

## The model (3.0.0)

- Drivers live in `ocos/drivers/storage/` - one file pair per
  family (`drv_storage_ahci.{c,h}`, `drv_storage_nvme.*`,
  `drv_storage_ata*`, `drv_storage_virtio_blk.*`, `drv_storage_scsi*`,
  `drv_storage_sdhci.*`).
- Identity is declared with `OC_DRIVER_DECL("block", "ahci",
  &ops, "SATA AHCI")` - a const descriptor the boot collector
  walks. No global registry edits, no init-call chains by hand.
- The driver mutates the block service's device table only from
  inside its own probe/detach path through the driver-internal API
  (`drv_storage_api.h`: `driver_block_add_device` /
  `remove_device`; `BLK_MAX_DEVICES` slots, 512-byte sectors,
  32-char names). The register-class entries were removed in
  v2.0.0 - this header is kernel-internal, NOT an L1 surface.
- The hardware profile knobs (`CONFIG_NVME` / `CONFIG_AHCI` /
  `CONFIG_VIRTIO_BLK`, `make defconfig` / `menuconfig`) compile
  individual families out; the init calls are macro-stubbed by
  `core/generated/config_gen.h`.

## The service side

`ocos/services/block/` fronts the L1SVC category 2 surface: device
listing (`lsblk`), partition tables (`parted`), mkfs (FAT32 /
exFAT / ext4), `fsck`, `sync`, the A/B disk tools (`abdisk`,
`install`, `grub-install`, `abcfg`), the disk stats, and the test
family (`ahci_test` .. `fs_mount_test`). The cache gate (64 slots)
sits between the service and the drivers.

## What a new family looks like

1. `ocos/drivers/storage/drv_storage_<fam>.{c,h}` - probe the bus,
   read the capacity, `driver_block_add_device()` per found disk.
2. One `OC_DRIVER_DECL("block", "<fam>", &ops, "description")`
   line at the bottom.
3. A status op (if the family deserves one) in the service, an op
   number APPENDED to `svc_sysapi_l1svc.h` (append-only!), the
   mirror regenerated (`tools/gen_l1svc_ops.py`).
4. The app side: a thin wrapper folder or a row in a mega-app's
   classic table (`disk-tool`'s `g_ops[]` is the pattern).

## The consumer contract

Apps never touch drivers: they call the service (`disk-tool lsblk`
-> `sys_l1svc(BLOCK, LSBLK, ...)`). Device names are the stable
contract (`hd0`, `hd0p1`, ...); the wizards show `lsblk` and ask
for names by that spelling.
