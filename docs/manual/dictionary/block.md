<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Block devices and disk tools (23)

### abcfg

| Field | Value |
|---|---|
| Name | abcfg |
| Category | Block devices and disk tools |
| Function | A/B slot configuration |
| Usage | `abcfg` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
abcfg
# abdisk boot/flags grub.cfg (slot A/B selection):

insmod fat
insmod part_msdos
insmod serial
set timeout=5
set default=0
serial --unit=0 --speed=115200
terminal_input console
terminal_output serial console

set slotid="A"
set slotroot="(hd0,msdos2)"
if [ -f (hd0,msdos1)/boot/next_B ]; then
  if [ -f (hd0,msdos1)/boot/bootfail_B ]; then
    set slotroot="(hd0,msdos2)"
    set slotid="A"
  else
    set slotroot="(hd0,msdos3)"
    set slotid="B"
  fi
else
  if [ -f (hd0,msdos1)/boot/ok_B ]; then
    set slotroot="(hd0,msdos3)"
    set slotid="B"
  fi
fi
echo "BOOT_SLOT=${slotid}"

menuentry "Open Cube OS (slot ${slotid})" {
  multiboot2 ${slotroot}/boot/opencube.elf oc.slot=${slotid}
  module2 ${slotroot}/boot/opencube.elf self
  boot
}
menuentry "Open Cube OS (slot A, force)" {
  multiboot2 (hd0,msdos2)/boot/opencube.elf oc.slot=A
  module2 (hd0,msdos2)/boot/opencube.elf self
  boot
}
menuentry "Open Cube OS (slot B, force)" {
  multiboot2 (hd0,msdos3)/boot/opencube.elf oc.slot=B
  module2 (hd0,msdos3)/boot/opencube.elf self
  boot
}
# install single-system grub.cfg:

insmod fat
insmod part_msdos
insmod serial
serial --unit=0 --speed=115200
terminal_input console
terminal_output serial console
set timeout=3
set default=0
menuentry "Open Cube OS" {
  multiboot2 /boot/opencube.elf oc.slot=ISO
  module2 /boot/opencube.elf self
  boot
}
ush> 
```

### abdisk

| Field | Value |
|---|---|
| Name | abdisk |
| Category | Block devices and disk tools |
| Function | A/B slot disk tool |
| Usage | `abdisk <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
abdisk
usage: abdisk <device>  (creates A/B update disk; DESTROYS existing data)
ush> 
```

### ahci

| Field | Value |
|---|---|
| Name | ahci |
| Category | Block devices and disk tools |
| Function | AHCI controller status |
| Usage | `ahci` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
ahci
ahci: no controller detected
ush> 
```

### ahci_test

| Field | Value |
|---|---|
| Name | ahci_test |
| Category | Block devices and disk tools |
| Function | AHCI port test |
| Usage | `ahci_test` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
ahci_test
[ahci_test] AHCI drive presence => FAIL (no AHCI drive registered)
ush> 
```

### ata

| Field | Value |
|---|---|
| Name | ata |
| Category | Block devices and disk tools |
| Function | ATA/IDE status |
| Usage | `ata` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
ata
ata_dma: controllers=1 dma_drives=0
  ctrl at PCI 00:01.1 BAR4=0xc000 dma_drive=none
  hda: PIO/absent
  hdb: PIO/absent
  hdc: PIO/absent
  hdd: PIO/absent
ush> 
```

### ata_dma_test

| Field | Value |
|---|---|
| Name | ata_dma_test |
| Category | Block devices and disk tools |
| Function | ATA DMA test |
| Usage | `ata_dma_test` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
ata_dma_test
[ata_dma_test] ATA drive presence => FAIL (no ATA drive registered)
ush> 
```

### disk_rw_test

| Field | Value |
|---|---|
| Name | disk_rw_test |
| Category | Block devices and disk tools |
| Function | disk read/write test |
| Usage | `disk_rw_test <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
disk_rw_test
usage: disk_rw_test <device>  (hda|sda|vda|nvme0|...)
ush> 
```

### dskstat

| Field | Value |
|---|---|
| Name | dskstat |
| Category | Block devices and disk tools |
| Function | disk I/O statistics |
| Usage | `dskstat` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
dskstat
ATA drive detection:
  primary master: absent
  primary slave: absent
  secondary master: absent
  secondary slave: absent
Block devices:
drivers: ATA-DMA drives=0  AHCI drives=0  NVMe queues=0
ush> 
```

### fs_mount_test

| Field | Value |
|---|---|
| Name | fs_mount_test |
| Category | Block devices and disk tools |
| Function | filesystem mount test |
| Usage | `fs_mount_test <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
fs_mount_test
usage: fs_mount_test <device>  (formats the device!)
  explicit argument required - never touches a device implicitly
ush> 
```

### fsck

| Field | Value |
|---|---|
| Name | fsck |
| Category | Block devices and disk tools |
| Function | check a filesystem |
| Usage | `fsck` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
fsck
fsck: checking mounted FAT32 filesystems
usage: fsck <device>
  e.g. fsck hda
ush> 
```

### grub-install

| Field | Value |
|---|---|
| Name | grub-install |
| Category | Block devices and disk tools |
| Function | install the OCBL boot chain |
| Usage | `grub-install <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
grub-install
usage: grub-install <device>  (write the GRUB BIOS boot loader)
ush> 
```

### install

| Field | Value |
|---|---|
| Name | install |
| Category | Block devices and disk tools |
| Function | install Open Cube OS to a disk |
| Usage | `install <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
install
usage: install <device>  (install the running system; DESTROYS existing data)
ush> 
```

### lsblk

| Field | Value |
|---|---|
| Name | lsblk |
| Category | Block devices and disk tools |
| Function | list block devices |
| Usage | `lsblk` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
lsblk
Block devices:
Disk cache: 0/64 slots  hits=0 misses=0
ush> 
```

### mkfs.exfat

| Field | Value |
|---|---|
| Name | mkfs.exfat |
| Category | Block devices and disk tools |
| Function | create an exFAT filesystem |
| Usage | `mkfs.exfat <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
mkfs.exfat
mkfs.exfat: WARNING: formatting DESTROYS ALL DATA on the target device!
  usage: mkfs.exfat <device> confirm
ush> 
```

### mkfs.ext4

| Field | Value |
|---|---|
| Name | mkfs.ext4 |
| Category | Block devices and disk tools |
| Function | create an ext4 filesystem |
| Usage | `mkfs.ext4 <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
mkfs.ext4
mkfs.ext4: WARNING: formatting DESTROYS ALL DATA on the target device!
  usage: mkfs.ext4 <device> confirm
ush> 
```

### mkfs.fat32

| Field | Value |
|---|---|
| Name | mkfs.fat32 |
| Category | Block devices and disk tools |
| Function | create a FAT32 filesystem |
| Usage | `mkfs.fat32 <dev>` |
| Parameters | `dev` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
mkfs.fat32
mkfs.fat32: WARNING: formatting DESTROYS ALL DATA on the target device!
  usage: mkfs.fat32 <device> confirm
ush> 
```

### nvme

| Field | Value |
|---|---|
| Name | nvme |
| Category | Block devices and disk tools |
| Function | NVMe controller status |
| Usage | `nvme` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
nvme
nvme: no controller online
ush> 
```

### nvme_test

| Field | Value |
|---|---|
| Name | nvme_test |
| Category | Block devices and disk tools |
| Function | NVMe namespace test |
| Usage | `nvme_test` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
nvme_test
[nvme_test] NVMe drive presence => FAIL (no NVMe drive registered)
ush> 
```

### parted

| Field | Value |
|---|---|
| Name | parted |
| Category | Block devices and disk tools |
| Function | partition table editor (view) |
| Usage | `parted [dev]` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
parted
no block devices
ush> 
```

### partition_test

| Field | Value |
|---|---|
| Name | partition_test |
| Category | Block devices and disk tools |
| Function | partition scan test |
| Usage | `partition_test [dev]` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
partition_test
usage: partition_test <device>
ush> 
```

### real_hw_test

| Field | Value |
|---|---|
| Name | real_hw_test |
| Category | Block devices and disk tools |
| Function | real hardware disk test |
| Usage | `real_hw_test` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
real_hw_test
[real_hw_test] input: check execution environment
[real_hw_test] actual: hypervisor bit set (QEMU/sandbox)
[real_hw_test] => NOT RUN (no physical storage or NIC in a VM;
    real-machine SATA/NVMe/ATA and WP-10b NIC validation
    needs bare metal)
[real_hw_test] --- WP-10u in-system update ---
[real_hw_test] A/B update flows are validated in QEMU
    (update_pkg_test, ab_partition_test, update_*_test,
    real_update_test with a real reboot); real-machine A/B
    validation (GRUB installed to a physical disk) NOT RUN
    in this sandbox
[real_hw_test] --- WP-10d USB (real emulated HC + devices) ---
USB: not present
  msc: none
  serial: none
  mouse: none
USB Audio: not present
ush> 
```

### sync

| Field | Value |
|---|---|
| Name | sync |
| Category | Block devices and disk tools |
| Function | flush block caches to disk |
| Usage | `sync` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
sync
disk cache flushed
ush> 
```

### virtio_blk_test

| Field | Value |
|---|---|
| Name | virtio_blk_test |
| Category | Block devices and disk tools |
| Function | virtio-blk test |
| Usage | `virtio_blk_test` |
| Source | `ocos/services/block/src/svc_block_l1.c` |

**Executed example**:

```
virtio_blk_test
[virtio_blk_test] virtio drive presence => FAIL (no virtio-blk device)
ush> 
```
