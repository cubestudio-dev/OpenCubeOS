<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 文件系统 (6)

### fatmount

| 字段 | 值 |
|---|---|
| 名称 | fatmount |
| 分类 | 文件系统 |
| 功能 | 挂载 FAT32 卷 |
| 用法 | `fatmount [dev p]   : mount FAT32` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/fs/src/svc_fs_l1.c` |

**实际执行示例**：

```
export DICTVAR=1
ush> 
```

### fatstat

| 字段 | 值 |
|---|---|
| 名称 | fatstat |
| 分类 | 文件系统 |
| 功能 | FAT32 卷统计 |
| 用法 | `fatstat            : FAT32 stats` |
| 返回码 | -1, 0 |
| 来源 | `ocos/services/fs/src/svc_fs_l1.c` |

**实际执行示例**：

```
env
PATH=/bin:/
HOME=/
PS1=ush> 
```

### mount

| 字段 | 值 |
|---|---|
| 名称 | mount |
| 分类 | 文件系统 |
| 功能 | 挂载文件系统/列出挂载 |
| 用法 | `mount [type dev p] : list or mount; mount <type> <device> <path>` |
| 参数 | `type`, `device`, `path` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/fs/src/svc_fs_l1.c` |

**实际执行示例**：

```
mount
VFS mount table:
  /  ramfs
ing DESTROYS ALL DATA on the target device!
  usage: mkfs.fat32 <device> confirm
ush> 
```

### mounts

| 字段 | 值 |
|---|---|
| 名称 | mounts |
| 分类 | 文件系统 |
| 功能 | 列出已挂载文件系统 |
| 用法 | `mounts             : list (alias` |
| 返回码 | 0 |
| 来源 | `ocos/services/fs/src/svc_fs_l1.c` |

**实际执行示例**：

```
mounts
VFS mount table:
  /  ramfs
ush> 
```

### tree

| 字段 | 值 |
|---|---|
| 名称 | tree |
| 分类 | 文件系统 |
| 功能 | 显示目录树 |
| 用法 | `tree [path]        : directory tree` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/fs/src/svc_fs_l1.c` |

**实际执行示例**：

```
tree
/
+ var/
  + log/
      kernel.log
+ bin/
+ dev/
+ tmp/
+ etc/
    opencube.conf
    motd

6 directories, 3 files
ush> 
```

### umount

| 字段 | 值 |
|---|---|
| 名称 | umount |
| 分类 | 文件系统 |
| 功能 | 卸载文件系统 |
| 用法 | `umount <path>      : unmount; umount <path>` |
| 参数 | `path` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/fs/src/svc_fs_l1.c` |

**实际执行示例**：

```
umount
usage: umount <path>
ush> 
```
