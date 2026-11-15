<!-- SPDX-License-Identifier: Apache-2.0 -->
# 文件系统

## VFS 层

内核 VFS 作为前端对接四种文件系统实现：

| 类型 | 读 | 写 | 适用场景 |
|------|------|-------|-----------------|
| ramfs | 是 | 是 | ISO 根——一切都在 RAM 中 |
| FAT32 | 是 | 是 | 与所有操作系统交换数据 |
| exFAT | 是 | 是 | 大容量可移动卷 |
| ext4 | 读* | 否* | 检视 Linux 磁盘 |

（*）本版本中 ext4 这一分支以挂载/读取为主。

## 日常文件操作

```
ush> files                  the wizard: ls / tree / df+du / cp / mv /
                            rm (confirmed) / edit / stat+chmod
ush> files edit /etc/opencube.conf
```

也可以直接使用经典程序——`ls`、`tree`、`df`、`du`、`cp`、`mv`、`rm`、`edit`、`stat`、`chmod`、`cat`、`mkdir`、`ln`、`wc`、`grep`、`head`、`tail`、`sort`、`awk`。它们是普通的注册表程序；`files` 是进入同一批二进制的第二扇门。

## 格式化与挂载

`disk-tool` 带你走完整条流程：

```
ush> disk-tool              -> list devices (lsblk)
                           -> partition table (parted)
                           -> create a filesystem (mkfs.fat32 / exfat /
                              ext4 - the device name is asked, retyped
                              and confirmed)
                           -> mount manager (list / mount / umount /
                              fatmount / fatstat)
```

经典形式：

```
ush> mkfs.fat32 hd0p1       (destroys data - no wizard, no extra prompt)
ush> mount fat32 hd0p1 /mnt
ush> umount /mnt
```

`fatmount` 是 FAT 专用的便捷命令（自动探测 FAT 卷），`fatstat` 报告 FAT 统计信息，`fsck` 检查文件系统。

## /etc 卷

`/etc/opencube.conf` 承载持久化配置（更新 URL、自动检查、在线更新、shell 语言）。ISO 引导时它位于 RAM 中，重启后重置；在已安装的系统上（或接入 `etc.img` 磁盘时）则持久保存：

```
ush> config list
ush> config get update_url
ush> config set online_update no
ush> config restore         (back to defaults)
```

## 路径与当前目录

`cd`、`pwd`、`ls` 的行为与 Unix 同名命令一致；路径以 `/` 分隔，是以 ramfs 根为起点的绝对路径。编辑器原地写回；`cp`/`mv`/`rm`/`chmod`/`stat` 采用直观的形式（`chmod <octal> <path>`）。

## 运行中系统里各路径的内容

```
/            ramfs root (the ISO content)
/etc         the config volume (persisted when etc.img is attached)
/mnt, /media conventional mount points
/bin, /lib   the embedded program registry is in-kernel; these
             paths exist for the dynamic-linker leg (ld.so, libfoo)
```
