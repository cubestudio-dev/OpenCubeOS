<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-05 Extension Interfaces

**WP-05 adds 4 new extension points (items 18-21). Total L1 surface is now 21.**

WP-05 introduces the Virtual File System (VFS) layer with a uniform `open / read / write / seek / close / stat / mkdir / rmdir / readdir / unlink / rename` API on top of pluggable file-system backends. Two backends ship in WP-05: `ramfs` (in-memory, read/write) and `fat32` (block-backed, read/write — write support completed in WP-07). The ATA/IDE PIO driver provides the 512-byte-sector storage layer that FAT32 sits on.

All WP-01..WP-04 interfaces remain unchanged. WP-05 interfaces are additive.

WP-05 also uses the WP-03 `shell_register_command()` API to register sixteen file-operation shell commands (`ls`, `cd`, `pwd`, `cat`, `mkdir`, `rmdir`, `touch`, `rm`, `mv`, `cp`, `echo`, `tree`, `df`, `du`, `mount`, `umount`). That API is documented in `EXTENSIONS_WP03.md` section 14 and is not repeated here.

---

## Interface 18: VFS operations

### Signature

```c
#include "fs_vfs.h"

void fs_vfs_init(void);

int  fs_vfs_register_fs(const char *name,
                     fs_vfs_fs_ops_t   *fs_ops,
                     fs_vfs_file_ops_t *file_ops,
                     fs_vfs_dir_ops_t  *dir_ops);

int  fs_vfs_mount (const char *fs_type, const char *mount_point, const char *device);
int  fs_vfs_umount(const char *mount_point);

int  fs_vfs_open   (const char *path, int flags);
int  fs_vfs_read   (int fd, void *buf, int size);
int  fs_vfs_write  (int fd, const void *buf, int size);
int  fs_vfs_seek   (int fd, int offset, int whence);
int  fs_vfs_close  (int fd);
int  fs_vfs_stat   (const char *path, fs_vfs_stat_t *st);
int  fs_vfs_mkdir  (const char *path);
int  fs_vfs_rmdir  (const char *path);
int  fs_vfs_readdir(const char *path, int index, fs_vfs_dirent_t *entry);
int  fs_vfs_unlink (const char *path);
int  fs_vfs_rename (const char *oldpath, const char *newpath);

fs_vfs_node_t *fs_vfs_resolve(const char *path);

int  fs_vfs_register_hook(int (*hook)(int op, const char *path));
void fs_vfs_list_mounts(void);

fs_vfs_node_t *fs_vfs_alloc_node(const char *name, int type, fs_vfs_fs_type_t *fs_type);
void        fs_vfs_attach_child(fs_vfs_node_t *parent, fs_vfs_node_t *child);
void        fs_vfs_detach_child(fs_vfs_node_t *child);
```

### Purpose

The VFS layer provides a single uniform API on top of multiple backing file systems. Each file-system type (ramfs, fat32, ...) registers a `fs_vfs_fs_type_t` with `mount` / `unmount` hooks plus a set of `file_ops` and `dir_ops`. Paths are resolved by walking the global node tree (`parent / first_child / next_sibling`), transparently crossing mount points.

A mount table maps an absolute path (e.g. `/`, `/mnt`) to the root node of the mounted file system. Path resolution finds the longest matching mount prefix and walks the rest of the components through `dir_ops->lookup`.

File descriptors are small integers (`>= 0`) indexing a fixed-size fd table (`VFS_MAX_FDS = 64`). Each fd holds a node pointer, flags, and a 64-bit offset. The VFS layer tracks the offset; the fs-specific `read`/`write` callbacks receive it so the fs implementation stays stateless per-open.

### Parameters

| Function | Description |
|---|---|
| `fs_vfs_init()` | Initialise the VFS subsystem (fd table, mount table, fs-type table). Called once at boot before any fs registers. |
| `fs_vfs_register_fs(name, fs_ops, file_ops, dir_ops)` | Register a new file-system type by `name` (≤ 15 chars). `fs_ops`, `file_ops`, `dir_ops` are pointers to const op tables stored by reference. Returns 0 on success, -1 on bad arg, -2 if `VFS_MAX_FS_TYPES` (8) reached. |
| `fs_vfs_mount(fs_type, mount_point, device)` | Mount the registered fs type `fs_type` at the absolute path `mount_point`. `device` is fs-specific (e.g. `"ata0"` for FAT32, ignored by ramfs). Returns 0 on success, negative on error. |
| `fs_vfs_umount(mount_point)` | Unmount the fs at `mount_point`. Returns 0 on success, negative on error. |
| `fs_vfs_open(path, flags)` | Open `path` for I/O. `flags` is a bitmask of `VFS_O_RDONLY`, `VFS_O_WRONLY`, `VFS_O_RDWR`, `VFS_O_CREAT`, `VFS_O_APPEND`. Returns fd `>= 0` on success, negative on error. |
| `fs_vfs_read(fd, buf, size)` | Read up to `size` bytes into `buf` from the current offset, advancing it. Returns the number of bytes read (0 at EOF), negative on error. |
| `fs_vfs_write(fd, buf, size)` | Write `size` bytes from `buf` at the current offset, advancing it. Returns the number of bytes written, negative on error. |
| `fs_vfs_seek(fd, offset, whence)` | Set the offset. `whence` is `VFS_SEEK_SET` / `VFS_SEEK_CUR` / `VFS_SEEK_END`. Returns the new offset on success, negative on error. |
| `fs_vfs_close(fd)` | Close the fd. Returns 0 on success, negative on error. |
| `fs_vfs_stat(path, st)` | Fill `*st` with type, size, and name for `path`. Returns 0 on success, negative on error. |
| `fs_vfs_mkdir(path)` | Create a directory at `path`. Returns 0 on success, negative on error. |
| `fs_vfs_rmdir(path)` | Remove the directory at `path` (must be empty). Returns 0 on success, negative on error. |
| `fs_vfs_readdir(path, index, entry)` | Read the `index`-th entry of directory `path` into `*entry`. Returns 0 on success, negative on error or end-of-directory. |
| `fs_vfs_unlink(path)` | Delete a regular file at `path`. Returns 0 on success, negative on error or unsupported. |
| `fs_vfs_rename(oldpath, newpath)` | Rename within the same file system. Returns 0 on success, negative on error or cross-fs. |
| `fs_vfs_resolve(path)` | Walk the node tree and return the `fs_vfs_node_t *` for `path` (NULL if not found). |
| `fs_vfs_register_hook(hook)` | Install a callback invoked on every open/close/read/write/mount/umount. Hook returns 0 = let VFS proceed, non-zero = VFS aborts the op. Returns 0 on success, -1 on bad arg, -2 if table full. |
| `fs_vfs_list_mounts()` | Print all active mounts to the console. |
| `fs_vfs_alloc_node(name, type, fs_type)` | Allocate + zero a `fs_vfs_node_t`, set its `name`, `type` (`VFS_TYPE_FILE` / `VFS_TYPE_DIR` / `VFS_TYPE_DEVICE`), and `fs_type`. Returns NULL on OOM. |
| `fs_vfs_attach_child(parent, child)` | Insert `child` into `parent`'s sibling list (maintains `parent->first_child` / `next_sibling`). |
| `fs_vfs_detach_child(child)` | Remove `child` from its parent's sibling list. |

### Open flags and seek whence

```c
#define VFS_O_RDONLY  0x0001
#define VFS_O_WRONLY  0x0002
#define VFS_O_RDWR    0x0003   /* RDONLY | WRONLY == RDWR */
#define VFS_O_CREAT   0x0004
#define VFS_O_APPEND  0x0008

#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2
```

### Node and dirent structs

```c
typedef struct {
    int  type;              /* VFS_TYPE_* */
    u64  size;
    char name[VFS_NAME_LEN];   /* VFS_NAME_LEN = 64 */
} fs_vfs_stat_t;

typedef struct {
    char name[VFS_NAME_LEN];
    int  type;
    u64  inode;             /* fs-specific inode number (0 if N/A) */
} fs_vfs_dirent_t;

struct fs_vfs_node {
    char             name[VFS_NAME_LEN];
    int              type;             /* VFS_TYPE_* */
    u64              size;             /* file size in bytes (0 for dirs) */
    fs_vfs_fs_type_t   *fs_type;          /* owning fs type */
    void            *private;          /* fs-specific data */
    fs_vfs_node_t      *parent;
    fs_vfs_node_t      *first_child;
    fs_vfs_node_t      *next_sibling;
};
```

### Return value

- All `int`-returning functions return 0 on success and a negative value on error.
- `fs_vfs_open` returns a non-negative file descriptor on success.
- `fs_vfs_read` / `fs_vfs_write` return the byte count transferred (0 on EOF for read).
- `fs_vfs_seek` returns the new absolute offset on success.
- `fs_vfs_resolve` and `fs_vfs_alloc_node` return a pointer (NULL on failure).

### Example

```c
#include "fs_vfs.h"

/* Write-then-read back through the VFS. */
void fs_vfs_demo(void) {
    int fd = fs_vfs_open("/tmp/test.txt", VFS_O_RDWR | VFS_O_CREAT);
    if (fd < 0) return;

    fs_vfs_write(fd, "hello, vfs\n", 11);
    fs_vfs_seek(fd, 0, VFS_SEEK_SET);

    char buf[16];
    int n = fs_vfs_read(fd, buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; /* ... */ }

    fs_vfs_close(fd);

    /* Directory iteration. */
    fs_vfs_dirent_t e;
    for (int i = 0; fs_vfs_readdir("/etc", i, &e) == 0; i++) {
        /* e.name, e.type, e.inode */
    }

    /* Stat a path. */
    fs_vfs_stat_t st;
    if (fs_vfs_stat("/tmp/test.txt", &st) == 0) {
        /* st.type == VFS_TYPE_FILE, st.size == 11 */
    }
}
```

### Registering a new file-system type (L1 driver pattern)

```c
static fs_vfs_node_t *my_mount(const char *device);
static int         my_unmount(fs_vfs_node_t *root);
static int         my_open (fs_vfs_node_t *node, int flags);
static int         my_read (fs_vfs_node_t *node, u64 off, void *buf, int size);
static int         my_write(fs_vfs_node_t *node, u64 off, const void *buf, int size);
static u64         my_seek (fs_vfs_node_t *node, u64 off, int whence);
static int         my_close(fs_vfs_node_t *node);
static int         my_stat (fs_vfs_node_t *node, fs_vfs_stat_t *st);
static int         my_mkdir  (fs_vfs_node_t *parent, const char *name);
static int         my_rmdir  (fs_vfs_node_t *parent, const char *name);
static int         my_readdir(fs_vfs_node_t *dir, int index, fs_vfs_dirent_t *entry);
static fs_vfs_node_t *my_lookup (fs_vfs_node_t *parent, const char *name);
static int         my_unlink (fs_vfs_node_t *parent, const char *name);
static int         my_rename (fs_vfs_node_t *parent, const char *oldn, const char *newn);

static fs_vfs_fs_ops_t   my_fs_ops   = { .mount = my_mount, .unmount = my_unmount };
static fs_vfs_file_ops_t my_file_ops = { .open = my_open, .read = my_read,
                                      .write = my_write, .seek = my_seek,
                                      .close = my_close, .stat = my_stat };
static fs_vfs_dir_ops_t  my_dir_ops  = { .mkdir = my_mkdir, .rmdir = my_rmdir,
                                      .readdir = my_readdir, .lookup = my_lookup,
                                      .unlink = my_unlink, .rename = my_rename };

void myfs_init(void) {
    fs_vfs_register_fs("myfs", &my_fs_ops, &my_file_ops, &my_dir_ops);
    fs_vfs_mount("myfs", "/mnt/myfs", "ata0");   /* device is fs-specific */
}
```

`unlink` and `rename` may be NULL for read-only file systems; the VFS layer rejects the corresponding shell command with `-ENOSYS` in that case. `rename` is only required to work within the same parent — the shell falls back to copy+unlink across directories or across file systems.

### Caveats

- **Single fd table, global**: all kernel threads share the same 64-entry fd table. There is no per-process fd table yet (user-mode processes get their own fd table in a later WP).
- **No concurrent mount/unmount protection**: callers should hold the kernel lock or stop the world if they need to safely mount/unmount while I/O is in flight.
- **Path length limit**: `VFS_PATH_LEN = 256` including the trailing NUL.
- **No symbolic links / hard links**: symlink traversal, `readlink`, `link()` are not yet supported.
- **No permissions / ownership**: `fs_vfs_stat_t` does not carry mode bits or uid/gid.

---

## Interface 19: ramfs (in-memory file system)

### Signature

```c
#include "fs_ramfs.h"

void fs_ramfs_init(void);
void fs_ramfs_get_stats(int *total_nodes, int *total_size);
```

### Purpose

`ramfs` is the in-memory, read/write file system mounted at `/` early in boot. It registers the `"ramfs"` fs type with VFS, mounts itself at `/`, and creates a small default directory tree (`/etc/`, `/tmp/`, `/dev/`). All file data is stored in kernel heap memory (`kmalloc`'d buffers), so contents disappear on reboot.

`fs_ramfs_init()` is called once at boot. After it returns, the root file system is immediately usable through the VFS API. File-system drivers that need a writable scratch space (e.g. for log files, configuration, or temporary mounts before the real disk is available) can rely on ramfs being up after `fs_ramfs_init()`.

### Parameters

| Function | Description |
|---|---|
| `fs_ramfs_init()` | Register the `"ramfs"` fs type with VFS and mount it at `/`. Creates `/etc`, `/tmp`, `/dev`. No return value (panics on OOM). |
| `fs_ramfs_get_stats(total_nodes, total_size)` | Query aggregate stats: `*total_nodes` receives the number of live VFS nodes (files + dirs), `*total_size` receives the total bytes stored across all files. Either pointer may be NULL to skip. |

### Return value

- `fs_ramfs_init`: no return value.
- `fs_ramfs_get_stats`: no return value (writes through the out-pointers).

### Example

```c
#include "fs_ramfs.h"
#include "fs_vfs.h"

void fs_ramfs_demo(void) {
    /* fs_ramfs_init() has already run at boot — "/" is ramfs. */

    int fd = fs_vfs_open("/tmp/note.txt", VFS_O_RDWR | VFS_O_CREAT);
    fs_vfs_write(fd, "scratch pad", 11);
    fs_vfs_close(fd);

    int nodes, size;
    fs_ramfs_get_stats(&nodes, &size);
    /* nodes >= 4 (root + /etc + /tmp + /dev + note.txt), size >= 11 */
}
```

### Caveats

- **Volatile**: contents are lost on reboot. For persistent storage use a block-backed fs (FAT32 on ATA, etc.).
- **Heap-backed**: a large ramfs file will exhaust the kernel heap. There is no per-file or per-mount size limit yet.
- **Single mount**: `fs_ramfs_init()` mounts ramfs only at `/`. It is possible to mount additional ramfs instances at other paths by calling `fs_vfs_mount("ramfs", "/mnt/extra", NULL)` after `fs_ramfs_init()`.

---

## Interface 20: ATA driver (LBA28 PIO)

### Signature

```c
#include "driver_block_ata.h"

void driver_block_ata_init(void);

int driver_block_ata_read_sectors (int drive, u64 lba, int count, void *buf);
int driver_block_ata_write_sectors(int drive, u64 lba, int count, const void *buf);
int driver_block_ata_detect       (int drive);
```

### Purpose

PIO (programmed I/O) ATA/IDE driver using LBA28 addressing, for the primary and secondary channels. Drives are addressed by a single integer `0..3`:

| Drive | Channel  | Select | I/O base | DEV head |
|-------|----------|--------|----------|----------|
| 0     | primary  | master | 0x1F0    | 0xE0     |
| 1     | primary  | slave  | 0x1F0    | 0xF0     |
| 2     | secondary| master | 0x170    | 0xE0     |
| 3     | secondary| slave  | 0x170    | 0xF0     |

`driver_block_ata_init()` probes all four drive slots at boot, calls `driver_block_ata_detect()` on each, and registers the present drives as VFS devices. Subsequent reads/writes go through the 512-byte-sector API.

This is the lowest-level disk interface in WP-05. In WP-07 it gets a thin wrapper (the block-device registry, see `EXTENSIONS_WP07.md`) so that ATA, virtio-blk, and NVMe all expose the same `driver_block_read_sectors` / `driver_block_write_sectors` shape.

### Parameters

| Function | Description |
|---|---|
| `driver_block_ata_init()` | Probe all four drive slots at boot via `driver_block_ata_detect()` and register each present drive. Called once at boot. No return value. |
| `driver_block_ata_read_sectors(drive, lba, count, buf)` | Read `count` 512-byte sectors starting at `lba` from `drive` (`0..3`) into `buf`. `buf` must be at least `count * 512` bytes. Returns the number of sectors actually read (== `count`) on success, or a negative value on error. |
| `driver_block_ata_write_sectors(drive, lba, count, buf)` | Write `count` 512-byte sectors from `buf` to `drive` starting at `lba`. Returns the number of sectors written on success, or a negative value on error. |
| `driver_block_ata_detect(drive)` | Probe whether `drive` is present. Returns 1 if the drive responds to IDENTIFY with a non-erasing signature, 0 otherwise. |

### Return value

- `driver_block_ata_init`: no return value.
- `driver_block_ata_read_sectors` / `driver_block_ata_write_sectors`: sector count transferred on success (`>= 0`), negative on error.
- `driver_block_ata_detect`: 1 if drive present, 0 otherwise.

### Example

```c
#include "driver_block_ata.h"

void driver_block_ata_demo(void) {
    /* driver_block_ata_init() has already run at boot. */

    if (!driver_block_ata_detect(0)) return;   /* no primary master */

    u8 mbr[512];
    int n = driver_block_ata_read_sectors(0, 0, 1, mbr);   /* read MBR */
    if (n != 1) return;                       /* I/O error */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;   /* bad signature */

    /* Write it back unchanged (just to exercise the write path). */
    driver_block_ata_write_sectors(0, 0, 1, mbr);
}
```

### Caveats

- **LBA28 only**: the largest addressable sector is `2^28 - 1` (128 GiB). LBA48 support will arrive in a later WP.
- **PIO, no DMA**: each sector is moved through `in`/`out` instructions. Throughput is limited (~1-3 MiB/s in QEMU). DMA / bus-mastering ATA is future work.
- **Polling, no IRQ**: the driver busy-waits on the status register (`BSY` / `DRQ`). It does not register an IRQ handler — IRQ14/15 are unused by the driver today.
- **No partition awareness at this layer**: the driver addresses the raw disk. Use the partition parser (`EXTENSIONS_WP07.md` section 29) to find file-system partitions, or mount a FAT32 by raw LBA via `fs_fat32_mount("ata0", ...)`.
- **Drive index is fixed at boot**: hot-plug is not supported.

---

## Interface 21: FAT32 driver

### Signature

```c
#include "fs_fat32.h"

void fs_fat32_init(void);
int  fs_fat32_mount(const char *device, const char *mount_point);
void fs_fat32_get_stats(u64 *total_sectors, u64 *free_clusters, u32 *cluster_size);
```

### Purpose

FAT32 file-system driver, sitting on top of ATA. The implementation caches the first FAT in memory and walks cluster chains on demand. WP-05 ships read support (mount + open + read + stat + readdir); WP-07 adds full write support (file creation, cluster allocation, chain extension, directory entry updates, file deletion).

`fs_fat32_init()` registers the `"fat32"` fs type with VFS but mounts nothing. `fs_fat32_mount()` is a convenience wrapper around `fs_vfs_mount("fat32", mount_point, device)` — it validates the boot sector, parses BPB, locates the FAT and root cluster, and attaches a root `fs_vfs_node_t` at `mount_point`.

`device` is the VFS device name registered by `driver_block_ata_init()` (e.g. `"ata0"`, `"ata1"`). The driver uses the VFS device layer (or, in WP-07 and later, the block-device layer) to read sectors from the underlying disk.

### Parameters

| Function | Description |
|---|---|
| `fs_fat32_init()` | Register the `"fat32"` fs type with VFS. Does NOT mount anything. No return value. Call once at boot before any `fs_fat32_mount()`. |
| `fs_fat32_mount(device, mount_point)` | Mount the FAT32 file system on `device` at the absolute path `mount_point`. `device` is e.g. `"ata0"`. Returns 0 on success, negative on error (bad device, not FAT32, I/O error). Equivalent to `fs_vfs_mount("fat32", mount_point, device)`. |
| `fs_fat32_get_stats(total_sectors, free_clusters, cluster_size)` | Query stats for the *currently mounted* FAT32 (whichever was mounted last). `*total_sectors` receives the volume size in sectors; `*free_clusters` receives the number of free clusters (counted by walking the FAT); `*cluster_size` receives the cluster size in bytes. Any pointer may be NULL to skip. If no FAT32 is mounted, all outputs are 0. |

### Return value

- `fs_fat32_init`: no return value.
- `fs_fat32_mount`: 0 on success, negative on error.
- `fs_fat32_get_stats`: no return value (writes through out-pointers).

### Example

```c
#include "fs_fat32.h"
#include "fs_vfs.h"

void fs_fat32_demo(void) {
    /* fs_fat32_init() has already run at boot. */

    if (fs_fat32_mount("ata0", "/mnt/disk") != 0) return;

    /* List the root of the mounted FAT32. */
    fs_vfs_dirent_t e;
    for (int i = 0; fs_vfs_readdir("/mnt/disk", i, &e) == 0; i++) {
        /* e.name, e.type, e.inode */
    }

    /* Read a file. */
    int fd = fs_vfs_open("/mnt/disk/README.TXT", VFS_O_RDONLY);
    if (fd >= 0) {
        char buf[128];
        int n = fs_vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; /* ... */ }
        fs_vfs_close(fd);
    }

    /* Write a file (requires WP-07 write support). */
    fd = fs_vfs_open("/mnt/disk/NEW.TXT", VFS_O_WRONLY | VFS_O_CREAT);
    if (fd >= 0) {
        fs_vfs_write(fd, "created by Open Cube OS\n", 24);
        fs_vfs_close(fd);
    }

    /* Query stats. */
    u64 total_sec, free_clusters;
    u32  cluster_size;
    fs_fat32_get_stats(&total_sec, &free_clusters, &cluster_size);
}
```

### Caveats

- **Read-only in WP-05**: write operations (`fs_vfs_write` to a FAT32 path, `fs_vfs_unlink`, `fs_vfs_rename` across FAT32 dirs) require WP-07. The WP-05 driver returns `-ENOSYS` for `unlink` / `rename` and refuses to create new files. After WP-07, all ops work.
- **First FAT cached only**: the FAT32 implementation caches FAT #0 in memory. FAT mirroring is not honoured — writes go to FAT #0 only (this matches typical SD-card and USB-stick usage).
- **Short (8.3) names only in WP-05**: VFAT long-name parsing arrives in a later WP. Files with LFN-only names show up as their short-name alias.
- **No fsck / journaling**: corruption from an unclean shutdown is not auto-repaired. Always `fs_vfs_umount()` before power-off.
- **Single mounted FAT32**: `fs_fat32_get_stats()` reports stats for whichever FAT32 was mounted last; mounting multiple FAT32 volumes at once is supported by `fs_vfs_mount()` but the stats helper only reflects the most recent one.

---

## ABI Stability

All WP-05 functions, structs, and their typedefs are frozen:

- **VFS (item 18)**: `fs_vfs_init`, `fs_vfs_register_fs`, `fs_vfs_mount`, `fs_vfs_umount`, `fs_vfs_open`, `fs_vfs_read`, `fs_vfs_write`, `fs_vfs_seek`, `fs_vfs_close`, `fs_vfs_stat`, `fs_vfs_mkdir`, `fs_vfs_rmdir`, `fs_vfs_readdir`, `fs_vfs_unlink`, `fs_vfs_rename`, `fs_vfs_resolve`, `fs_vfs_register_hook`, `fs_vfs_list_mounts`, `fs_vfs_alloc_node`, `fs_vfs_attach_child`, `fs_vfs_detach_child` — signatures frozen.
  - Struct layouts frozen: `fs_vfs_stat_t`, `fs_vfs_dirent_t`, `fs_vfs_fs_ops_t`, `fs_vfs_file_ops_t`, `fs_vfs_dir_ops_t`, `fs_vfs_fs_type`, `fs_vfs_node`, `fs_vfs_file`.
  - Constants frozen: `VFS_TYPE_FILE/DIR/DEVICE`, `VFS_O_RDONLY/WRONLY/RDWR/CREAT/APPEND`, `VFS_SEEK_SET/CUR/END`, `VFS_HOOK_OPEN/CLOSE/READ/WRITE/MOUNT/UMOUNT`.
  - Capacity limits frozen: `VFS_MAX_FS_TYPES = 8`, `VFS_MAX_MOUNTS = 16`, `VFS_MAX_FDS = 64`, `VFS_NAME_LEN = 64`, `VFS_PATH_LEN = 256`.
- **ramfs (item 19)**: `fs_ramfs_init`, `fs_ramfs_get_stats` — signatures frozen.
- **ATA (item 20)**: `driver_block_ata_init`, `driver_block_ata_read_sectors`, `driver_block_ata_write_sectors`, `driver_block_ata_detect` — signatures frozen. The `0..3` drive-index convention and the LBA28 PIO behaviour are frozen.
- **FAT32 (item 21)**: `fs_fat32_init`, `fs_fat32_mount`, `fs_fat32_get_stats` — signatures frozen.

The struct layouts are frozen with respect to the fields documented above. New fields may be appended at the end in future WPs; callers must zero-initialise structs before passing them in to allow this. The `fs_vfs_node_t.private` pointer is fs-specific and explicitly **not** part of the ABI — its layout depends on the registered fs type.

Future WPs may add new functions and new file-system types but will not change existing ones incompatibly.

## Loading model

WP-05 still does not have a dynamic module loader — L1 is linked into the same binary as L0. The extension API is designed to survive the transition to loadable modules unchanged: a loadable fs driver would just call `fs_vfs_register_fs()` from its module-init function.
