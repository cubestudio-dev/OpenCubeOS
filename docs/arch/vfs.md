<!-- SPDX-License-Identifier: Apache-2.0 -->
# VFS: the mount table, the node tree, and four filesystems

The virtual filesystem is `ocos/services/fs/src/svc_fs_vfs.c` with the
family drivers `svc_fs_{ramfs,fat32,exfat,ext4}.c` and the config
backend `svc_fs_config_backend.c`. Limits: `VFS_MAX_FS_TYPES 8`,
`VFS_MAX_MOUNTS 16`, `VFS_MAX_FDS 64`, `VFS_NAME_LEN 64`,
`VFS_PATH_LEN 256`. Node types: FILE 1, DIR 2, DEVICE 3, SYMLINK 4
(WP-10-wp08fix1). Open flags: RDONLY/WRONLY/RDWR/CREAT/APPEND/TRUNC;
default modes 0644/0755.

## The on-wire structs (verbatim)

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

Ops tables: `fs_vfs_fs_ops_t{mount, unmount}`;
`fs_vfs_file_ops_t{open, read, write, seek, close, stat}` (the VFS
tracks offset); `fs_vfs_dir_ops_t{mkdir, rmdir, readdir, lookup, unlink, rename, create, link, symlink, readlink, move}` - each entry
may be NULL when the backing filesystem does not support it (e.g.
read-only ext4 unlink; disk filesystems leave cross-dir `move` NULL so
the caller falls back to copy+unlink). Hooks:
`VFS_HOOK_OPEN/CLOSE/READ/WRITE/MOUNT/UMOUNT` (1-6).

## Mount flow and the refusal paths

`fs_vfs_mount(fs_type, mount_point, device)`: find the longest matching
mount prefix, `mkdir`-ensure the parent path exists, call
`fs_ops->mount(device)` to obtain a root node, and map the absolute
path to it in the mount table. Refusals, each with its exact string:

- `"vfs_mount: fs mount failed"` - the driver rejected the device.
- `"vfs_mount: parent path does not exist"`.
- `"vfs_mount: mount point exists and is not a directory"`.

`fs_vfs_umount` refuses with: -4 = root filesystem
(`"umount: cannot unmount the root filesystem"`), -5 = busy
(`"umount: target is busy - close open files first"`), -3 = nested
mounts first (`"umount: unmount nested mounts first"`, the BUG-0133
reason surfaced by the L1 op), else
`"umount: failed (not mounted?)"`.

`fs_vfs_resolve()` walks path components through `dir_ops->lookup`,
crossing mount points transparently and following VFS_TYPE_SYMLINK
nodes. **Case rule (BUG-0179/A12-014)**: the node cache must match
names the same way the backing filesystem does -
`fs_vfs_set_fs_case_insensitive("fat32"/"exfat", 1)`.

The L1 surface (`mount`, `mounts`, `umount`, `fatmount`, `fatstat`,
`tree`): `mount` with no args lists via `fs_vfs_list_mounts_str`;
`mount <type> <device> <path>` pre-creates the mountpoint (v1 parity);
`fatmount` does **not** pre-create (the P2-18 fix) and on failure
prints the honest two-line message including "(the mount point was NOT
created - `ls <mnt>` will report 'no such path' until a real mount
succeeds)". `tree` caps depth at 16 with "..." at depth >= 12
(BUG-0087). Relative paths resolve against the **calling process's
cwd** (`fspath_resolve_base(proc->cwd, ...)`), with the kernel
fallback `fspath_resolve`.

## File descriptors and the fd table

`VFS_MAX_FDS 64`. The process-side `user_proc_t.fds[]` carries the
kind of each descriptor: **0 = free, 1 = VFS fd, 2 = pipe-read,
3 = pipe-write** - one namespace for files and pipes, which is what
makes `dup`/`dup2` and shell redirection work
([syscall-ring3.md](syscall-ring3.md)). The `fs_vfs_file.refcnt`
field (BUG-0098) keeps a dup'd/fork-inherited fd alive until the last
close; `open_count` on the node (BUG-0061) is what `umount`'s busy
check reads.

## Path resolution

`fs_vfs_resolve()` walks the components from the root (or the
process cwd) through `dir_ops->lookup`, crossing mount points
transparently and following symlink nodes. Per-process cwd is the
rule: `SYS_CHDIR`/`SYS_GETCWD` maintain it, and every relative path
in a syscall resolves against the **calling process's** cwd
(`fspath_resolve_base(proc->cwd, ...)`), with the kernel-side
fallback `fspath_resolve` for internal callers. Hard links bump
`nlink`; rename inside one directory is a node re-parent; cross-dir
`move` is left NULL by the disk filesystems so the caller falls back
to copy+unlink.

## The four filesystems

| Filesystem | Mode | Implementation notes |
|---|---|---|
| **ramfs** | RW | `svc_fs_ramfs.c` - registers "ramfs", mounts at `/` in kmain, creates the default `/etc /tmp /dev` tree. **The node tree IS the storage.** The only filesystem implementing the full dir-ops set including link/symlink/move. Failure: `"ramfs: mount at / failed"`. |
| **FAT32** | RW | `svc_fs_fat32.c` - caches the first FAT in memory, walks cluster chains on demand. Write support: file creation, cluster allocation, chain extension, directory-entry updates, deletion. Mount refusals: `"fat32: drive not present"`, `"fat32: read boot sector failed rc="`, `"fat32: not a FAT32 volume (FAT12/16 detected)"`, `"fat32: FAT cache alloc failed"`, `"fat32: FAT read failed"`. |
| **ext4** | **RO** | `svc_fs_ext4.c` - read-only. 4 KiB blocks, extents understood. Mount refusals: `"ext4: drive not present"`, `"ext4: read superblock failed"`, `"ext4: not an ext4 volume (bad magic)"`, `"ext4: read root inode failed"`. (Writable ext4 exists in the tree, but in the *installer* - `ocse/ocse_ext4w.c` - not in the main kernel.) |
| **exFAT** | RW | `svc_fs_exfat.c` - read/write. Mount refusals: `"exfat: drive not present"`, `"exfat: read boot sector failed"`, `"exfat: not an exFAT volume"`, `"exfat: warning - volume was not cleanly unmounted"`. |

The config backend (`svc_fs_config_backend.c`) gives the
`/etc/opencube.conf` key=value store (`core_lib_config.c`) a VFS-backed
sink; `lib_config_init_with(fs_config_backend())` in kmain.

## Numbers at a glance

| Quantity | Value |
|---|---|
| Filesystem types | 8 max (4 registered: ramfs, fat32, ext4, exfat) |
| Mounts | 16 max |
| File descriptors | 64 max (shared with pipe ends) |
| Name / path length | 64 / 256 bytes |
| Node types | FILE, DIR, DEVICE, SYMLINK |
| Hooks | OPEN/CLOSE/READ/WRITE/MOUNT/UMOUNT (1-6) |
| Default tree | `/` + `/etc /tmp /dev` (ramfs, from kmain) |

## Who talks to VFS

- **Syscalls**: the file group (open/close/stat/readdir/mkdir/rmdir/
  unlink/read/write2/symlink/readlink/link/chmod/chown) and the
  fd-aware pipe/dup layer ([syscall-ring3.md](syscall-ring3.md)).
- **Ring 3**: `l1_fs_api.h`'s on-wire structs
  ([private-headers.md](private-headers.md)) - `ush_dirent`/
  `ush_stat` MUST match the kernel shapes byte-for-byte.
- **klog**: the /var/log/kernel.log file channel + rotation
  ([../internals/klog.md](../internals/klog.md)); **crash**: core
  dumps to /var/crash and crashlog flushes to /var/log/crash.log
  ([crash-recovery.md](crash-recovery.md)); **health**: JSON exports;
  **OTA**: package downloads into /data and slot-B installs
  ([ota-ab.md](ota-ab.md)); **fonts/TUI**: file pickers.
- **Block layer**: the fs drivers mount *devices*, never drivers -
  the partition parser exposes `<disk>pN` children with bounds
  enforcement ([driver-model.md](driver-model.md)).

User-facing guide: [../manual/filesystems.md](../manual/filesystems.md).
