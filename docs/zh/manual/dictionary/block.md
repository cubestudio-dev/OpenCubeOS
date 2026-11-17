<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 块设备与磁盘工具 (23)

### abcfg

| 字段 | 值 |
|---|---|
| 名称 | abcfg |
| 分类 | 块设备与磁盘工具 |
| 功能 | A/B 分区配置 |
| 用法 | `abcfg` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

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

| 字段 | 值 |
|---|---|
| 名称 | abdisk |
| 分类 | 块设备与磁盘工具 |
| 功能 | A/B 分区磁盘工具 |
| 用法 | `abdisk <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
abdisk
usage: abdisk <device>  (creates A/B update disk; DESTROYS existing data)
ush> 
```

### ahci

| 字段 | 值 |
|---|---|
| 名称 | ahci |
| 分类 | 块设备与磁盘工具 |
| 功能 | AHCI 控制器状态 |
| 用法 | `ahci` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
ahci
ahci: no controller detected
ush> 
```

### ahci_test

| 字段 | 值 |
|---|---|
| 名称 | ahci_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | AHCI 端口测试 |
| 用法 | `ahci_test` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
ahci_test
[ahci_test] AHCI drive presence => FAIL (no AHCI drive registered)
ush> 
```

### ata

| 字段 | 值 |
|---|---|
| 名称 | ata |
| 分类 | 块设备与磁盘工具 |
| 功能 | ATA/IDE 状态 |
| 用法 | `ata` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

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

| 字段 | 值 |
|---|---|
| 名称 | ata_dma_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | ATA DMA 测试 |
| 用法 | `ata_dma_test` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
ata_dma_test
[ata_dma_test] ATA drive presence => FAIL (no ATA drive registered)
ush> 
```

### disk_rw_test

| 字段 | 值 |
|---|---|
| 名称 | disk_rw_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | 磁盘读写测试 |
| 用法 | `disk_rw_test <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
disk_rw_test
```

### dskstat

| 字段 | 值 |
|---|---|
| 名称 | dskstat |
| 分类 | 块设备与磁盘工具 |
| 功能 | 磁盘 I/O 统计 |
| 用法 | `dskstat` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
dskstat
```

### fs_mount_test

| 字段 | 值 |
|---|---|
| 名称 | fs_mount_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | 文件系统挂载测试 |
| 用法 | `fs_mount_test <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
fs_mount_test
usage: fs_mount_test <device>  (formats the device!)
  explicit argument required - never touches a device implicitly
ush> 
```

### fsck

| 字段 | 值 |
|---|---|
| 名称 | fsck |
| 分类 | 块设备与磁盘工具 |
| 功能 | 检查文件系统 |
| 用法 | `fsck` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
fsck
fsck: checking mounted FAT32 filesystems
usage: fsck <device>
  e.g. fsck hda
ush> 
```

### grub-install

| 字段 | 值 |
|---|---|
| 名称 | grub-install |
| 分类 | 块设备与磁盘工具 |
| 功能 | 安装 OCBL 引导链 |
| 用法 | `grub-install <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
grub-install
usage: grub-install <device>  (write the GRUB BIOS boot loader)
ush> 
```

### install

| 字段 | 值 |
|---|---|
| 名称 | install |
| 分类 | 块设备与磁盘工具 |
| 功能 | 把系统安装到磁盘 |
| 用法 | `install <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
install
usage: install <device>  (install the running system; DESTROYS existing data)
ush> 
```

### lsblk

| 字段 | 值 |
|---|---|
| 名称 | lsblk |
| 分类 | 块设备与磁盘工具 |
| 功能 | 列出块设备 |
| 用法 | `lsblk` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
lsblk
Block devices:
Disk cache: 0/64 slots  hits=0 misses=0
ush> 
```

### mkfs.exfat

| 字段 | 值 |
|---|---|
| 名称 | mkfs.exfat |
| 分类 | 块设备与磁盘工具 |
| 功能 | 创建 exFAT 文件系统 |
| 用法 | `mkfs.exfat <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
mkfs.exfat
mkfs.exfat: WARNING: formatting DESTROYS ALL DATA on the target device!
  usage: mkfs.exfat <device> confirm
ush> 
```

### mkfs.ext4

| 字段 | 值 |
|---|---|
| 名称 | mkfs.ext4 |
| 分类 | 块设备与磁盘工具 |
| 功能 | 创建 ext4 文件系统 |
| 用法 | `mkfs.ext4 <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
mkfs.ext4
mkfs.ext4: WARNING: formatting DESTROYS ALL DATA on the target device!
  usage: mkfs.ext4 <device> confirm
ush> 
```

### mkfs.fat32

| 字段 | 值 |
|---|---|
| 名称 | mkfs.fat32 |
| 分类 | 块设备与磁盘工具 |
| 功能 | 创建 FAT32 文件系统 |
| 用法 | `mkfs.fat32 <dev>` |
| 参数 | `dev` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
mkfs.fat32
mkfs.fat32: WARNING: formatting DESTROYS ALL DATA on the target device!
  usage: mkfs.fat32 <device> confirm
ush> 
```

### nvme

| 字段 | 值 |
|---|---|
| 名称 | nvme |
| 分类 | 块设备与磁盘工具 |
| 功能 | NVMe 控制器状态 |
| 用法 | `nvme` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
nvme
nvme: no controller online
ush> 
```

### nvme_test

| 字段 | 值 |
|---|---|
| 名称 | nvme_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | NVMe 命名空间测试 |
| 用法 | `nvme_test` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
nvme_test
[nvme_test] NVMe drive presence => FAIL (no NVMe drive registered)
ush> 
```

### parted

| 字段 | 值 |
|---|---|
| 名称 | parted |
| 分类 | 块设备与磁盘工具 |
| 功能 | 分区表查看 |
| 用法 | `parted [dev]` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
parted
no block devices
ush> 
```

### partition_test

| 字段 | 值 |
|---|---|
| 名称 | partition_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | 分区扫描测试 |
| 用法 | `partition_test [dev]` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
partition_test
usage: partition_test <device>
ush> 
```

### real_hw_test

| 字段 | 值 |
|---|---|
| 名称 | real_hw_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | 真实硬件磁盘测试 |
| 用法 | `real_hw_test` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
real_hw_test
```

### sync

| 字段 | 值 |
|---|---|
| 名称 | sync |
| 分类 | 块设备与磁盘工具 |
| 功能 | 把块缓存刷入磁盘 |
| 用法 | `sync` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
sync
disk cache flushed
ush> 
```

### virtio_blk_test

| 字段 | 值 |
|---|---|
| 名称 | virtio_blk_test |
| 分类 | 块设备与磁盘工具 |
| 功能 | virtio 块设备测试 |
| 用法 | `virtio_blk_test` |
| 来源 | `ocos/services/block/src/svc_block_l1.c` |

**实际执行示例**：

```
virtio_blk_test
[virtio_blk_test] virtio drive presence => FAIL (no virtio-blk device)
ush> 
```
