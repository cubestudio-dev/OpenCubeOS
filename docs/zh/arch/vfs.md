<!-- SPDX-License-Identifier: Apache-2.0 -->
# VFS：挂载表、节点树与四个文件系统

虚拟文件系统是 `ocos/services/fs/src/svc_fs_vfs.c`，家族驱动为
`svc_fs_{ramfs,fat32,exfat,ext4}.c`，配置后端为
`svc_fs_config_backend.c`。限制：`VFS_MAX_FS_TYPES 8`、
`VFS_MAX_MOUNTS 16`、`VFS_MAX_FDS 64`、`VFS_NAME_LEN 64`、
`VFS_PATH_LEN 256`。节点类型：FILE 1、DIR 2、DEVICE 3、SYMLINK 4
（WP-10-wp08fix1）。打开标志：RDONLY/WRONLY/RDWR/CREAT/APPEND/
TRUNC；默认模式 0644/0755。

## 线上传输结构体（逐字）

```c
typedef struct {
    char name[VFS_NAME_LEN];
    int  type;                          /* VFS_TYPE_* */
    u64  inode;                         /* fs-specific inode number (0 if N/A) */
} fs_vfs_dirent_t;

struct fs_vfs_fs_type {
    char             name[16];
    fs_vfs_fs_ops_t    *fs_ops;
    fs_vfs_file_ops_t  *file_ops;
    fs_vfs_dir_ops_t   *dir_ops;
    int              case_insensitive;  /* BUG-0179: 1 = FAT32/exFAT, 0 = ramfs/ext4 */
};

struct fs_vfs_node {
    char             name[VFS_NAME_LEN];
    int              type;             /* VFS_TYPE_* */
    u64              size;             /* file size in bytes (0 for dirs) */
    u32              mode;
    u32              uid;
    u32              gid;
    u32              nlink;            /* hard link count (ramfs maintains) */
    int              open_count;       /* fds currently open on node (BUG-0061) */
    fs_vfs_fs_type_t   *fs_type;
    void            *private;          /* fs-specific data */
    fs_vfs_node_t      *parent;
    fs_vfs_node_t      *first_child;
    fs_vfs_node_t      *next_sibling;
};

struct fs_vfs_file {
    fs_vfs_node_t   *node;
    int           flags;
    u64           offset;
    int           in_use;
    int           refcnt;              /* BUG-0098: dup/fork-inheritance safety */
};
```

Ops 表：`fs_vfs_fs_ops_t{mount, unmount}`；
`fs_vfs_file_ops_t{open, read, write, seek, close, stat}`（偏移由
VFS 跟踪）；`fs_vfs_dir_ops_t{mkdir, rmdir, readdir, lookup, unlink, rename, create, link, symlink, readlink, move}`——当后端
文件系统不支持某项时，对应条目可为 NULL（例如只读 ext4 的
unlink；磁盘文件系统把跨目录 `move` 留空，使调用者回退到
copy+unlink）。钩子：`VFS_HOOK_OPEN/CLOSE/READ/WRITE/MOUNT/UMOUNT`
（1-6）。

## 挂载流程与拒绝路径

`fs_vfs_mount(fs_type, mount_point, device)`：查找最长匹配的挂载
前缀，用 `mkdir` 确保父路径存在，调用 `fs_ops->mount(device)`
取得根节点，并在挂载表中把绝对路径映射到它。各拒绝情形及其确切
字符串：

- `"vfs_mount: fs mount failed"`——驱动拒绝了该设备。
- `"vfs_mount: parent path does not exist"`。
- `"vfs_mount: mount point exists and is not a directory"`。

`fs_vfs_umount` 的拒绝：-4 = 根文件系统
（`"umount: cannot unmount the root filesystem"`）、-5 = 忙
（`"umount: target is busy - close open files first"`）、-3 = 先
卸载嵌套挂载（`"umount: unmount nested mounts first"`，BUG-0133
的原因经 L1 op 浮出水面），否则
`"umount: failed (not mounted?)"`。

`fs_vfs_resolve()` 经 `dir_ops->lookup` 逐段走路径，透明跨越挂载
点并跟踪 VFS_TYPE_SYMLINK 节点。**大小写规则（BUG-0179/A12-014）**：
节点缓存必须以后端文件系统的方式匹配名字——
`fs_vfs_set_fs_case_insensitive("fat32"/"exfat", 1)`。

L1 面（`mount`、`mounts`、`umount`、`fatmount`、`fatstat`、
`tree`）：无参 `mount` 经 `fs_vfs_list_mounts_str` 列出；
`mount <type> <device> <path>` 预先创建挂载点（v1 对等）；
`fatmount` **不**预先创建（P2-18 修复），失败时打印诚实的两行
消息，包括 "(the mount point was NOT created - `ls <mnt>` will
report 'no such path' until a real mount succeeds)"。`tree` 深度
上限 16，深度 >= 12 时显示 "..."（BUG-0087）。相对路径对照
**调用进程的 cwd** 解析（`fspath_resolve_base(proc->cwd, ...)`），
内核侧回退为 `fspath_resolve`。

## 文件描述符与 fd 表

`VFS_MAX_FDS 64`。进程侧 `user_proc_t.fds[]` 记录每个描述符的
种类：**0 = 空闲、1 = VFS fd、2 = 管道读、3 = 管道写**——文件与
管道共用一个命名空间，这正是 `dup`/`dup2` 与 shell 重定向得以
工作的原因（见 [syscall-ring3.md](syscall-ring3.md)）。
`fs_vfs_file.refcnt` 字段（BUG-0098）让被 dup/fork 继承的 fd 存活
到最后一次关闭；节点上的 `open_count`（BUG-0061）是 `umount`
忙检查所读的值。

## 路径解析

`fs_vfs_resolve()` 从根（或进程 cwd）起经 `dir_ops->lookup` 逐段
走路径，透明跨越挂载点并跟踪符号链接节点。每进程 cwd 是规则：
`SYS_CHDIR`/`SYS_GETCWD` 维护它，系统调用中的每个相对路径都对照
**调用进程的** cwd 解析（`fspath_resolve_base(proc->cwd, ...)`），
内核内部调用者则回退到 `fspath_resolve`。硬链接递增 `nlink`；
同一目录内的 rename 是节点重新挂父；跨目录 `move` 被磁盘文件系统
留空，使调用者回退到 copy+unlink。

## 四个文件系统

| 文件系统 | 模式 | 实现说明 |
|---|---|---|
| **ramfs** | RW | `svc_fs_ramfs.c`——注册 "ramfs"，在 kmain 中挂载于 `/`，创建默认的 `/etc /tmp /dev` 树。**节点树即存储。**唯一实现完整 dir-ops 集（含 link/symlink/move）的文件系统。失败：`"ramfs: mount at / failed"`。 |
| **FAT32** | RW | `svc_fs_fat32.c`——把第一份 FAT 缓存在内存，按需走簇链。写支持：文件创建、簇分配、链延伸、目录项更新、删除。挂载拒绝：`"fat32: drive not present"`、`"fat32: read boot sector failed rc="`、`"fat32: not a FAT32 volume (FAT12/16 detected)"`、`"fat32: FAT cache alloc failed"`、`"fat32: FAT read failed"`。 |
| **ext4** | **RO** | `svc_fs_ext4.c`——只读。4 KiB 块，理解 extents。挂载拒绝：`"ext4: drive not present"`、`"ext4: read superblock failed"`、`"ext4: not an ext4 volume (bad magic)"`、`"ext4: read root inode failed"`。（树中存在可写的 ext4，但在*安装器*里——`ocse/ocse_ext4w.c`——不在主内核。） |
| **exFAT** | RW | `svc_fs_exfat.c`——读/写。挂载拒绝：`"exfat: drive not present"`、`"exfat: read boot sector failed"`、`"exfat: not an exFAT volume"`、`"exfat: warning - volume was not cleanly unmounted"`。 |

配置后端（`svc_fs_config_backend.c`）为
`/etc/opencube.conf` 的 key=value 存储（`core_lib_config.c`）提供
VFS 后备的落点；kmain 中的 `lib_config_init_with(fs_config_backend())`。

## 数字速览

| 量 | 值 |
|---|---|
| 文件系统类型 | 最多 8（已注册 4：ramfs、fat32、ext4、exfat） |
| 挂载 | 最多 16 |
| 文件描述符 | 最多 64（与管道端共享） |
| 名字/路径长度 | 64 / 256 字节 |
| 节点类型 | FILE、DIR、DEVICE、SYMLINK |
| 钩子 | OPEN/CLOSE/READ/WRITE/MOUNT/UMOUNT（1-6） |
| 默认树 | `/` + `/etc /tmp /dev`（ramfs，来自 kmain） |

## 谁与 VFS 对话

- **系统调用**：文件组（open/close/stat/readdir/mkdir/rmdir/
  unlink/read/write2/symlink/readlink/link/chmod/chown）与 fd 感知
  的管道/dup 层（见 [syscall-ring3.md](syscall-ring3.md)）。
- **Ring 3**：`l1_fs_api.h` 的线上传输结构体
  （[private-headers.md](private-headers.md)）——`ush_dirent`/
  `ush_stat` 必须与内核形态逐字节一致。
- **klog**：/var/log/kernel.log 文件通道 + 轮转
  （见 [../internals/klog.md](../internals/klog.md)）；**crash**：
  核心转储到 /var/crash、crashlog 冲刷到 /var/log/crash.log
  （见 [crash-recovery.md](crash-recovery.md)）；**health**：JSON
  导出；**OTA**：包下载到 /data 与槽位 B 安装
  （见 [ota-ab.md](ota-ab.md)）；**字体/TUI**：文件选择器。
- **块层**：fs 驱动挂载的是*设备*，绝不是驱动——分区解析器暴露
  带边界强制的 `<disk>pN` 子设备
  （见 [driver-model.md](driver-model.md)）。

面向用户的指南：
[../manual/filesystems.md](../manual/filesystems.md)。
