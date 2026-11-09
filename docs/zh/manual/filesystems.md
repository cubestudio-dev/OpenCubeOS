<!-- SPDX-License-Identifier: Apache-2.0 -->
# 文件系统与存储

## VFS

所有文件访问都通过一个 VFS（`fs/fs_vfs.c`）进行：这是一个统一的
open/read/write/seek/close/stat/mkdir/rmdir/readdir/lookup API，支持
可插拔的文件系统类型。路径遍历全局节点树并透明地跨越
挂载点。每个文件系统的大小写敏感性、强制权限位和
64 位寻址偏移量都是真实的 VFS 行为。

## 文件系统类型

| 类型 | 读取 | 写入 | 存储位置 |
|------|------|-------|----------------|
| ramfs | 是 | 是 | 内存（易失）- 默认根文件系统 |
| FAT32 | 是 | 是 | 任何块设备；支持长文件名 |
| exFAT | 是 | 是 | 任何块设备 |
| ext4 | 是 | 是 | 任何块设备（基于扩展） |
| /etc 卷 | 是 | 是 | 挂载在 /etc 的小型 FAT32 镜像 |

```
oc> dskstat                     (list block devices)
oc> mkfs.fat32 ata0 confirm     (format - the confirm arg is required)
oc> mkfs.exfat usda confirm
oc> mkfs.ext4 ata0 confirm
oc> fatmount ata0 /mnt          (mount FAT32)
oc> mount                       (list mounts; umount unmounts)
```

格式化是破坏性操作 - 每个 `mkfs.*` 都需要字面意义的
`confirm` 参数，以防意外的 Enter 键擦除磁盘。

## /etc 卷

一个 32 MiB 的 FAT32 镜像，保存系统的持久状态：

```
opencube.conf     (key=value system configuration - see `config`)
ssh host keys     (sshd identity - see [network](network.md))
kernel.log        (rotated kernel log - see [../internals/klog.md])
crash dumps       (see [recovery](recovery.md))
```

配置的读写通过 `config` 命令和
L1 `l1_ext_config_*` 接口族进行；/etc 卷在启动时
如果存在会自动挂载。

## 块设备

块层为实例命名（`ata0`、`usda`、`nvme0`、`vdisk0`
用于 virtio，`sd0` 用于 SD），并在其上提供
分区扫描：

- **ATA/ATAPI** PIO 和总线主控 DMA（PRDT 对齐由
  堆的 32 字节头块保证）
- **AHCI** - SATA 控制器
- **NVMe** - PCIe SSD
- **virtio-blk** - QEMU 下的快速路径（`-device virtio-blk-pci`）
- **USB MSC** - 通过任何 USB 栈的
  大容量存储设备
- **SD/SDHCI** 和 **SCSI**（LSI 53C8xx 系列）控制器

## 分区表

扫描 GPT 和 MBR（msdos）分区表；分区设备
与整盘设备一起出现。支持 `fdisk` 风格的检查
和每个分区的挂载 - 请参阅磁盘域的
[命令参考](commands/index.md)。

## 文件命令

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

## 值得了解的限制

- 长路径和文件名：FAT32 LFN 完全支持，包括
  生成带冲突扫描的别名，以及在读取时进行校验和/序列
  验证。
- FAT32 维护双 FAT 镜像和 FSINFO 空闲簇提示；
  exFAT 跟踪活动的 FAT。
- ramfs 稀疏空洞读回为零。
- 拒绝 `umount /`；挂载阴影不会复制子项。
