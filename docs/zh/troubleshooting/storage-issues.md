<!-- SPDX-License-Identifier: Apache-2.0 -->
# 存储排查

按序排查、挂载拒绝路径、文件系统能力
矩阵、A/B 磁盘、以及写回缓存。

## 按序排查

运行 `disk-tool` 走菜单，或使用经典拼写：

```
ush> lspci                     (PCI storage controllers present?)
ush> ahci / ata / nvme         (family probe - the message names the fault)
ush> dskstat                   (block instances registered? cache state)
ush> lsblk                     (devices + partitions scanned?)
ush> parted <dev>              (the partition table)
ush> fatstat <dev>             (FAT statistics of a suspect volume)
ush> fsck <dev>                (filesystem check - FAT32 only)
ush> fatmount <dev> /mnt       (mount)
ush> fstest                    (the VFS/fs assertion suite)
```

## 文件系统能力矩阵

| 文件系统 | 读 | 写 | 备注 |
|------------|------|-------|-------|
| ramfs | RW | RW | rootfs；其上一切皆易失 — 只有 /etc 与已挂载卷持久 |
| FAT32 | RW | RW | 完整写支持（文件创建、簇分配、链扩展、目录更新、删除）；在内存中缓存第一份 FAT |
| exFAT | RW | RW | 不干净挂载时的脏标志警告（`exfat: warning - volume was not cleanly unmounted`） |
| ext4 | **RO** | — | 只读驱动；拒绝 INCOMP 特性位、> 64 KiB 块大小、奇数 inode 大小 |
| ext2/3 | RO | — | ext2 是 ext4 的子集；ext3 日志回放不执行 — 挂载为尽力而为的 RO |

大小写敏感性跟随底层 fs（BUG-0179）：FAT32/exFAT
大小写不敏感；ramfs/ext4 大小写敏感。VFS 限制：16 个
挂载、64 个 fd、名字 64 字节、路径 256 字节。

## 挂载拒绝路径（每个拒绝的含义）

- `vfs_mount: unknown fs type` — 类型不是
  ramfs/fat32/exfat/ext4 之一。
- `vfs_mount: parent path does not exist` / `mount point exists and is not a directory` — 创建挂载点（`mount <type> <dev> <path>`
  会预先创建；`fatmount` 刻意不创建 — fatmount
  失败时两行的如实消息会说明挂载点未被
  创建）。
- `fat32: not a FAT32 volume (FAT12/16 detected)` / `invalid BPB` /
  `read boot sector failed rc=<N>` — 该卷不是可挂载的
  FAT32。`fatstat <dev>` 显示解析的 BPB；常见原因是
  设备节点错误（`lsblk`） — 或者破坏对齐的堆
  回归（已修复；若你动过 mem_heap.c 请重跑
  `ata_dma_test`）。
- `fat32: FAT cache alloc failed` / `FAT read failed` — 内存或
  介质问题。
- `ext4: not an ext4 volume (bad magic)` / `read superblock failed` —
  不是 ext4，或超级块超出驱动可达范围。
- `ext4: unsupported INCOMP feature bits 0x<hex> - mount refused`（及
  块大小/inode 大小/组大小防护） — RO 驱动拒绝其
  无法兑现的特性；挂载一个更简单的卷。
- `exfat: invalid number_of_fats` — 不是 exFAT。
- `umount: target is busy - close open files first` — 打开的 fd
  扣住它（open_count 按节点跟踪，BUG-0061）。
- `umount: unmount nested mounts first` — 先子挂载（`mounts`
  显示嵌套）。
- `umount: cannot unmount the root filesystem` — 按设计。
- `fatmount` 之后的 `mount: failed` — 如实的两行消息
  以 `no FAT32 partition on device or drive not present` 结尾。

## 经典案例

| 症状 | 可能原因 |
|---------|--------------|
| 有效的 FAT32 卷被拒（`invalid BPB`） | 设备节点错误，或破坏对齐的堆回归（已修复；若动过 mem_heap.c 重跑 `ata_dma_test`） |
| 重启后写入丢失 | 它活在 ramfs 上 — 只有 /etc 与已挂载卷持久 |
| U 盘出现后又消失 | USB MSC 拔出路径 — `usb_hotplug_test` 可复现 |
| 对 ext2 卷做 ext4 挂载 | 受支持（ext2 是子集）；先检查 `fsck` 类信号 — fsck 本身仅支持 FAT32 |
| 向导两次询问设备名 | 按设计 — mkfs/abdisk/install 路径要求重输 + 裸 `y` 确认 |
| `abdisk` 之后 A/B 磁盘"absent" | 重跑 `update status`；向导循环复查直到服务看到它 |
| 干净卸载后 fsck 报脏 | FSINFO/FAT 镜像回归 — 报告它；这类是 bug |
| `blk: write to protected device ... REFUSED (ocire recovery disk)` | 整盘 OCIRE 保护 — 按设计；仅从 OCIRE `repair` 修改该卷 |
| `blkcache: alloc FAILED - cache disabled (raw I/O)` | 64 槽缓存无法分配；系统继续无缓存（更慢但正确） |
| `virtio-blk: write rejected (device is read-only)` | QEMU 驱动器以 `readonly=on` 挂载 — 按设计 |
| AHCI 端口上的 ATAPI 设备 | `<port>: ATAPI (not supported)` — 已声明的限制 |

## A/B 磁盘 — 布局与修复

由 `abdisk <dev>` 创建的布局（destructive；之后重跑
`ota_ab_rescan()`）：

```
p1 boot/flags 32 MB FAT32   /boot/next_B, /boot/ok_B, /boot/bootfail_B, grub.cfg
p2 slot A      128 MB FAT32 /boot/opencube.elf, /etc, /docs...
p3 slot B      128 MB FAT32 same structure as slot A
p4 /data       rest  FAT32 user data, downloaded packages
```

- **A/B 磁盘缺失**是第 3 号 OTA 静默失败：`update status`
  缺 `A/B disk: present`；`update` 报告
  `update: cannot determine target slot (not booted from A/B?)`。
- **修复**：`lsblk` → 选设备 → `abdisk <dev>`（classic，或
  disk-tool 向导的防护路径：它询问设备、要求你
  重输确切名字、警告、并要求裸 `y`）。该链
  报告自己的步骤失败（`abdisk: failed to write partition table`、`mkfs.fat32 failed on p<N>`、... — 第一处才是
  真凶）。
- 回滚语义：`rollback` 清除标志；引导菜单的
  ` rollback` 标签意味着 `next_B` + `bootfail_B` 强制槽 A。
- 引导尝试预算：`/boot/grub/grubenv` 的 `ab_attempts`，上限 3。

槽位选择状态机见 [boot-failures.md](boot-failures.md) §6。

## 64 槽 LRU 写回缓存

块层维护一个 64 槽（64×512 B = 32 KiB）LRU **写回**
缓存：`{dev_idx, lba, data[512], dirty, last_used}`，统计
`{hits, misses, evicts, writebacks, used}`（`dskstat` 显示
图景）。

这对你意味着：

- 写入的数据可能停留在缓存中直到逐出或刷写 —
  在电源操作、你在乎的卸载、以及任何
  会复位机器的实验之前运行
  `sync`。
- `sync` 驱动 OS 缓存刷写**外加**逐设备刷写钩子：
  ATA FLUSH CACHE、NVMe Flush (0x0A)、virtio-blk FLUSH、AHCI FLUSH
  CACHE EXT、SCSI SYNCHRONIZE CACHE。
- 如果缓存无法分配（`blkcache: alloc FAILED - cache disabled (raw I/O)`），写入直通（正确，更慢）。

## 破坏性路径

`mkfs.*`、`abdisk`、`install` 摧毁数据。在向导中它们
受防护（重输设备名，然后裸 `y` — 只有 `y`/`Y`
算作 yes）；经 classic/direct 分发则立即执行 —
与历史命令一模一样 — 因此脚本保持其
语义。**在 classic 形式之前对照 `lsblk` 双重核对设备名。**
`fs_mount_test <device>` 也会格式化其目标。
安装器链（`install <device>`，≥ 64 MiB，MBR ≤ 2 TiB）与
`grub-install`（MBR 间隙 ≥ core.img 大小）逐步报告失败
— [error-catalog.md](error-catalog.md) §5.4。

## 驱动级失败串

- AHCI：`ahci: issue fail (PxIS.TFE)`、`(PxTFD.ERR)`、
  `ahci: issue TIMEOUT CI=...`、`<port>: IDENTIFY failed`、
  `ahci: BAR5 invalid`。
- ATA：`ata: PIO read DRQ timeout lba=<n>`、`atadma: xfer FAIL rc=<n>`。
- NVMe：`nvme: BAR0 invalid`、`disable timeout ... retrying`、
  `ready timeout, resetting and retrying`、`unsupported formatted LBA size (only 512B supported)`、`Identify Controller/Namespace failed`。
- virtio-blk：`no legacy PCI device found (modern-only devices not supported)`、`used-ring poll timeout`。
- USB MSC：`msc: CBW bulk out failed`、`msc: CSW bad signature or tag`、`usb-msc: device not ready`。

含应对的完整清单：[error-catalog.md](error-catalog.md) §6.5。

## 解读探针

- `dskstat` — 哪些块实例已注册，外加缓存
  图景（64 槽 LRU 的 `hits/misses/evicts/writebacks/used`）。
  零实例 = 无存储驱动绑定（见下文驱动串
  / QEMU flag）。
- `lsblk` — 设备与扫描到的分区；设备名的权威来源
  （每条破坏性命令都精确采用这些名字）。
- `parted <dev>` — MBR/GPT 表视图。
- `fatstat <dev>` — 可疑 FAT32 的解析 BPB：如果字段
  （每扇区字节、每簇扇区、保留、FAT 大小、根簇）
  看起来荒谬，你要么看错了设备，要么卷损坏。
- `fsck <dev>` — 仅 FAT32 的检查；判定为逐项 `FAIL (<detail>)` 行加最终汇总（脏簇检查打印
  `cluster count mismatch: counted <A> ...`）。

## QEMU 存储 flag 速查

| 目标 | flag | 备注 |
|------|------|-------|
| 普通可写磁盘 | `-drive file=hd.img,format=raw` | 显示为 IDE/ATA |
| virtio-blk 快速路径 | `-drive file=hd.img,if=virtio,format=raw` | 需要遗留 PCI 设备（modern-only 被拒） |
| AHCI | `-device ahci` + `-drive ...` | ATAPI 设备打印 `not supported` |
| NVMe | `-drive file=nvme.img,if=none,id=nvme0 -device nvme,drive=nvme0,serial=ns1` | 仅支持 512B LBA 大小 |
| 只读意图 | `readonly=on` | 写入打印如实的 `virtio-blk: write rejected` |
| U 盘 | `-usb -device usb-storage,drive=u0` | 经 `usb_hotplug_test` 拔出/重接 |

记住 `-snapshot`（`run-*` make 目标中的默认值）意味着
写入不持久；`run-bios-persist` / `run-uefi-persist` 关闭
它，使 `/etc/opencube.conf` 的修改落到 `build/etc.img`。

## 分区表说明

- 安装路径仅 MBR（`install` 拒绝 > 2 TiB）。
- GPT 仅为显示而解析（`parted`） — 系统内安装器
  写入 MBR 布局。
- OCIRE 恢复盘的 0xEE"影子" MBR 条目不带 GPT
  头是刻意的伪装：常规 GPT 工具会忽略其
  后的 16 MiB FAT12 卷，整盘写保护
  使其保持如此。
- A/B 磁盘的分区子项（p2/p3）被槽位占用 — 它们
  无法注册自己的引导条目。

## 相关页面

- [error-catalog.md](error-catalog.md) §5.2-§5.4、§6.5 — 每条消息
- [../manual/filesystems.md](../manual/filesystems.md) — 用户指南
- [../manual/update-ota.md](../manual/update-ota.md) — A/B 的故事
- [boot-failures.md](boot-failures.md) — A/B 的引导侧
