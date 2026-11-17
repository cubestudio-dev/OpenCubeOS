<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - File systems (6)

### fatmount

| Field | Value |
|---|---|
| Name | fatmount |
| Category | File systems |
| Function | mount a FAT32 volume |
| Usage | `fatmount [dev p]   : mount FAT32` |
| Return codes | 0, 1 |
| Source | `ocos/services/fs/src/svc_fs_l1.c` |

**Executed example**:

```
fatmount
fat32: invalid device string
vfs_mount: fs mount failed
fatmount: ata0 -> /mnt FAILED - no FAT32 partition on device or drive not present
(the mount point was NOT created - `ls /mnt` will report 'no such path' until a real mount succeeds)
ush> 
```

### fatstat

| Field | Value |
|---|---|
| Name | fatstat |
| Category | File systems |
| Function | FAT32 volume statistics |
| Usage | `fatstat            : FAT32 stats` |
| Return codes | -1, 0 |
| Source | `ocos/services/fs/src/svc_fs_l1.c` |

**Executed example**:

```
fatstat
fat32: sectors=0 free_clusters=0 cluster_size=0
ush> 
```

### mount

| Field | Value |
|---|---|
| Name | mount |
| Category | File systems |
| Function | mount a filesystem / list mounts |
| Usage | `mount [type dev p] : list or mount; mount <type> <device> <path>` |
| Parameters | `type`, `device`, `path` |
| Return codes | 0, 1 |
| Source | `ocos/services/fs/src/svc_fs_l1.c` |

**Executed example**:

```
mount
VFS mount table:
  /  ramfs
ing DESTROYS ALL DATA on the target device!
  usage: mkfs.fat32 <device> confirm
ush> 
```

### mounts

| Field | Value |
|---|---|
| Name | mounts |
| Category | File systems |
| Function | list mounted filesystems |
| Usage | `mounts             : list (alias` |
| Return codes | 0 |
| Source | `ocos/services/fs/src/svc_fs_l1.c` |

**Executed example**:

```
mounts
VFS mount table:
  /  ramfs
ush> 
```

### tree

| Field | Value |
|---|---|
| Name | tree |
| Category | File systems |
| Function | show a directory tree |
| Usage | `tree [path]        : directory tree` |
| Return codes | 0, 1 |
| Source | `ocos/services/fs/src/svc_fs_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | umount |
| Category | File systems |
| Function | unmount a filesystem |
| Usage | `umount <path>      : unmount; umount <path>` |
| Parameters | `path` |
| Return codes | 0, 1 |
| Source | `ocos/services/fs/src/svc_fs_l1.c` |

**Executed example**:

```
umount
usage: umount <path>
ush> 
```
