<!-- SPDX-License-Identifier: Apache-2.0 -->
# 块驱动

一个存储设备家族如何加入块服务，让整个系统（VFS、分区、fs
工具、disk-tool 向导）看见它。

## 模型（3.0.0）

- 驱动住在 `ocos/drivers/storage/` - 每个家族一对文件
  （`drv_storage_ahci.{c,h}`、`drv_storage_nvme.*`、
  `drv_storage_ata*`、`drv_storage_virtio_blk.*`、
  `drv_storage_scsi*`、`drv_storage_sdhci.*`）。
- 身份用 `OC_DRIVER_DECL("block", "ahci", &ops, "SATA AHCI")`
  声明 - 一个由启动收集器遍历的 const 描述符。没有全局注册表
  编辑，没有手工的 init-call 链。
- 驱动只能在自己的探测/摘除路径内，通过驱动内部 API
  （`drv_storage_api.h`：`driver_block_add_device` /
  `remove_device`；`BLK_MAX_DEVICES` 个槽、512 字节扇区、32 字符
  名字）修改块服务的设备表。寄存器类条目已在 v2.0.0 移除 - 这个
  头是内核内部的，不是 L1 表面。
- 硬件配置旋钮（`CONFIG_NVME` / `CONFIG_AHCI` /
  `CONFIG_VIRTIO_BLK`，`make defconfig` / `menuconfig`）把单个
  家族编译出去；init 调用由 `core/generated/config_gen.h` 宏
  桩化。

## 服务侧

`ocos/services/block/` 代理 L1SVC 类别 2 表面：设备列表
（`lsblk`）、分区表（`parted`）、mkfs（FAT32 / exFAT / ext4）、
`fsck`、`sync`、A/B 盘工具（`abdisk`、`install`、
`grub-install`、`abcfg`）、磁盘统计，以及测试家族
（`ahci_test` .. `fs_mount_test`）。缓存门禁（64 槽）位于服务与
驱动之间。

## 一个新家族长什么样

1. `ocos/drivers/storage/drv_storage_<fam>.{c,h}` - 探测总线、
   读容量、每发现一块盘 `driver_block_add_device()` 一次。
2. 底部一行 `OC_DRIVER_DECL("block", "<fam>", &ops, "description")`。
3. 服务里一个状态操作（如果这个家族配得上的话），操作号追加到
   `svc_sysapi_l1svc.h`（只许追加！），重新生成镜像
   （`tools/gen_l1svc_ops.py`）。
4. 应用侧：一个薄包装文件夹或大应用经典表里的一行
   （`disk-tool` 的 `g_ops[]` 是样板）。

## 消费者契约

应用从不触碰驱动：它们调用服务（`disk-tool lsblk` ->
`sys_l1svc(BLOCK, LSBLK, ...)`）。设备名是稳定契约（`hd0`、
`hd0p1`、...）；向导展示 `lsblk` 并按那种拼法询问名字。
