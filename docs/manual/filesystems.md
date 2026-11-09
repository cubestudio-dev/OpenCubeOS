<!-- SPDX-License-Identifier: Apache-2.0 -->
# Filesystems and storage

## The VFS

All file access goes through one VFS (`fs/fs_vfs.c`): a uniform
open/read/write/seek/close/stat/mkdir/rmdir/readdir/lookup API over
pluggable filesystem types. Paths walk the global node tree and cross
mount points transparently. Per-filesystem case sensitivity, enforced
permission bits, and 64-bit seek offsets are real VFS behaviours.

## Filesystem types

| Type | Read | Write | Where it lives |
|------|------|-------|----------------|
| ramfs | yes | yes | memory (volatile) - the default root |
| FAT32 | yes | yes | any block device; LFN supported |
| exFAT | yes | yes | any block device |
| ext4 | yes | yes | any block device (extent-based) |
| /etc volume | yes | yes | a small FAT32 image mounted at /etc |

```
oc> dskstat                     (list block devices)
oc> mkfs.fat32 ata0 confirm     (format - the confirm arg is required)
oc> mkfs.exfat usda confirm
oc> mkfs.ext4 ata0 confirm
oc> fatmount ata0 /mnt          (mount FAT32)
oc> mount                       (list mounts; umount unmounts)
```

Formatting is destructive - every `mkfs.*` requires the literal
`confirm` argument so a stray Enter does not erase a disk.

## The /etc volume

A 32 MiB FAT32 image holding the system's persistent state:

```
opencube.conf     (key=value system configuration - see `config`)
ssh host keys     (sshd identity - see [network](network.md))
kernel.log        (rotated kernel log - see [../internals/klog.md])
crash dumps       (see [recovery](recovery.md))
```

Configuration reads/writes go through the `config` command and the
L1 `l1_ext_config_*` interface family; the /etc volume is mounted
automatically at boot when present.

## Block devices

The block layer names instances (`ata0`, `usda`, `nvme0`, `vdisk0`
for virtio, `sd0` for SD) and provides partition scanning on top:

- **ATA/ATAPI** PIO and bus-master DMA (PRDT alignment guaranteed by
  the heap's 32-byte header block)
- **AHCI** - SATA controllers
- **NVMe** - PCIe SSDs
- **virtio-blk** - the fast path under QEMU (`-device virtio-blk-pci`)
- **USB MSC** - mass-storage sticks through any of the USB stacks
- **SD/SDHCI** and **SCSI** (LSI 53C8xx family) controllers

## Partition tables

GPT and MBR (msdos) partition tables are scanned; partition devices
appear alongside whole-disk devices. `fdisk`-style inspection plus
per-partition mount is available - see the
[commands reference](commands/index.md) for the disk domain.

## File commands

```
oc> ls /mnt
oc> cat /mnt/hello.txt
oc> write /mnt/hello.txt "new content"
oc> cp /mnt/a.txt /mnt/b.txt
oc> mv /mnt/a.txt /mnt/c.txt
oc> rm /mnt/c.txt
oc> mkdir /mnt/dir
oc> touch /mnt/empty.txt
oc> editor /mnt/notes.txt        (full-screen editor)
oc> fsck ata0                     (filesystem check)
oc> fstest                        (VFS + per-fs self-test suite)
```

## Limits worth knowing

- Long paths and file names: FAT32 LFN is fully supported including
  alias generation with collision scanning and checksum/sequence
  validation on read.
- Dual-FAT mirror maintenance and FSINFO free-cluster hints are
  maintained on FAT32; exFAT tracks the active FAT.
- ramfs sparse holes read back as zeros.
- `umount /` is refused; mount shadowing does not duplicate children.
