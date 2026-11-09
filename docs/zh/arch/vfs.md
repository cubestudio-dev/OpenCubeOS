<!-- SPDX-License-Identifier: Apache-2.0 -->
# VFS 与文件系统家族

## VFS 契约

`fs/fs_vfs.{c,h}`：一个 API - open/read/write/seek/close/stat/mkdir/rmdir/readdir/lookup - 适用于已注册的文件系统类型。一种类型注册挂载/卸载钩子以及 file_ops 和 dir_ops。全局节点树透明地解析路径并跨越挂载点。

实际行为：节点缓存中的大小写敏感（因文件系统而异），强制权限位（模式 0000 被拒绝），按位写访问检查，i64 寻址，拒绝 `umount /`，无重复子节点的挂载遮蔽，有界的 `list_mounts`。

## FAT32 (`fs/fs_fat32.c`)

支持长文件名（LFN）与别名生成 + 碰撞扫描；读取时进行校验和与序列号验证；跨簇长文件名删除并回溯；维护双 FAT 镜像；读取并维护 FSINFO，并在其中保存下一个空闲簇提示。

## exFAT (`fs/fs_exfat.c`)

活动 FAT 跟踪、大写表处理、簇链。

## ext4 (`fs/fs_ext4.c`)

基于扩展区的读写、块组、inode 表。

## ramfs (`fs/fs_ramfs.c`)

易失性根文件系统；稀疏空洞读回时返回零。

## /etc 分区

一个挂载在 /etc（配置、SSH 密钥、日志、崩溃转储）上的 32 MiB FAT32 镜像 - 由 Makefile 根据 `etc/opencube.conf` 构建。它是为这个 otherwise in-memory system（内存系统）提供持久化的解决方案。

## 测试

`fstest`（双向断言套件）、`fsck`、`fatstat`、`fs_mount_test`，以及 `tests/` 中的宿主侧测试工具（使用 ASAN 的 FAT 结构解析）。回归测试套件在每次启动时运行 fstest。
