/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/vfs.h
 * Purpose: Virtual File System abstraction layer.
 *
 * The VFS provides a uniform API (open/read/write/seek/close/stat/mkdir/
 * rmdir/readdir/lookup) over multiple backing file systems. Each file system
 * type (ramfs, fat32, ...) registers a fs_vfs_fs_type_t with mount/unmount hooks
 * and a set of file_ops/dir_ops. Paths are resolved by walking the global
 * node tree, transparently crossing mount points.
 *
 * Design notes:
 *   - fs_vfs_node_t is the public face of every file/dir/device. It owns the
 *     parent/child/sibling pointers that form the global tree. fs-specific
 *     data hangs off the `private` pointer.
 *   - A mount table maps an absolute path (e.g. "/", "/mnt") to the root
 *     node of the mounted file system. Path resolution finds the longest
 *     matching mount prefix and walks the rest of the components through
 *     dir_ops->lookup.
 *   - File descriptors are small integers (>= 0) indexing a fixed-size fd
 *     table. Each fd holds a node pointer, flags, and a 64-bit offset.
 */
#ifndef OC_VFS_H
#define OC_VFS_H

#include "types.h"

/* ---- Node types ---- */
#define VFS_TYPE_FILE    1
#define VFS_TYPE_DIR     2
#define VFS_TYPE_DEVICE  3
/* WP-10-wp08fix1: symbolic links. The link target is stored fs-specific
 * (ramfs keeps it in the inode); fs_vfs_resolve() follows links when a
 * path component resolves to a node of this type. */
#define VFS_TYPE_SYMLINK 4

/* Default permission bits applied by fs_vfs_alloc_node(). chmod/chown
 * change them per node (RAM-persistent; on-disk filesystems keep their
 * own on-disk attributes when they have any). */
#define VFS_DEFAULT_FILE_MODE 0644
#define VFS_DEFAULT_DIR_MODE  0755

/* ---- Open flags (bitmask) ---- */
#define VFS_O_RDONLY  0x0001
#define VFS_O_WRONLY  0x0002
#define VFS_O_RDWR    0x0003   /* convenience: RDONLY | WRONLY == RDWR */
#define VFS_O_CREAT   0x0004
#define VFS_O_APPEND  0x0008
#define VFS_O_TRUNC   0x0010   /* P2-15: truncate existing file to 0 on open */

/* ---- Seek whence ---- */
#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

/* ---- Capacity limits ---- */
#define VFS_MAX_FS_TYPES  8
#define VFS_MAX_MOUNTS    16
#define VFS_MAX_FDS       64
#define VFS_NAME_LEN      64
#define VFS_PATH_LEN      256

/* Forward declarations. */
struct fs_vfs_node;
struct fs_vfs_file;
struct fs_vfs_fs_type;

typedef struct fs_vfs_node      fs_vfs_node_t;
typedef struct fs_vfs_file      fs_vfs_file_t;
typedef struct fs_vfs_fs_type   fs_vfs_fs_type_t;

/* Result of stat() and readdir().
 * WP-10-wp08fix1: grew mode/uid/gid/nlink. NOTE: this struct is copied
 * verbatim to user space by SYS_STAT - when changing it, mirror the new
 * layout in userprogs/ush.c (struct ush_stat). */
typedef struct {
    int  type;                          /* VFS_TYPE_* */
    u64  size;
    u32  mode;                          /* permission bits (0644 style) */
    u32  uid;
    u32  gid;
    u32  nlink;                         /* hard link count (ramfs) */
    char name[VFS_NAME_LEN];
} fs_vfs_stat_t;

typedef struct {
    char name[VFS_NAME_LEN];
    int  type;                          /* VFS_TYPE_* */
    u64  inode;                         /* fs-specific inode number (0 if N/A) */
} fs_vfs_dirent_t;

/* File system operations: mount/unmount. */
typedef struct {
    fs_vfs_node_t *(*mount)(const char *device);   /* returns root node or NULL */
    int         (*unmount)(fs_vfs_node_t *root);   /* returns 0 on success */
} fs_vfs_fs_ops_t;

/* File operations on a node. The VFS layer tracks the offset; read/write
 * receive it so the fs impl stays stateless per-open. */
typedef struct {
    int (*open )(fs_vfs_node_t *node, int flags);
    int (*read )(fs_vfs_node_t *node, u64 offset, void *buf, int size);
    int (*write)(fs_vfs_node_t *node, u64 offset, const void *buf, int size);
    u64 (*seek )(fs_vfs_node_t *node, u64 offset, int whence);   /* returns new offset */
    int (*close)(fs_vfs_node_t *node);
    int (*stat )(fs_vfs_node_t *node, fs_vfs_stat_t *st);
} fs_vfs_file_ops_t;

/* Directory operations. */
typedef struct {
    int         (*mkdir  )(fs_vfs_node_t *parent, const char *name);
    int         (*rmdir  )(fs_vfs_node_t *parent, const char *name);
    int         (*readdir)(fs_vfs_node_t *dir, int index, fs_vfs_dirent_t *entry);
    fs_vfs_node_t *(*lookup )(fs_vfs_node_t *parent, const char *name);
    /* WP-05 shell: unlink a regular file (remove from parent + free inode).
     * Returns 0 on success, negative on error. May be NULL (fs does not
     * support unlink, e.g. read-only FAT32). */
    int         (*unlink )(fs_vfs_node_t *parent, const char *name);
    /* WP-05 shell: rename a child within the same parent. May be NULL (the
     * shell falls back to copy+unlink). Returns 0 on success. */
    int         (*rename )(fs_vfs_node_t *parent, const char *oldname, const char *newname);
    /* P1-2 FIX: create a regular file (not a directory). Used by fs_vfs_open
     * with O_CREAT so the on-disk entry has file attributes (not dir).
     * May be NULL — VFS falls back to mkdir + type patch (ramfs style). */
    int         (*create )(fs_vfs_node_t *parent, const char *name);
    /* WP-10-wp08fix1: hard link `old_node` as `name` under `parent`.
     * ramfs shares the private inode and bumps the nlink count.
     * May be NULL (fs does not support hard links, e.g. FAT32). */
    int         (*link   )(fs_vfs_node_t *parent, const char *name,
                           fs_vfs_node_t *old_node);
    /* WP-10-wp08fix1: create a symbolic link `name` under `parent`
     * pointing at `target` (copied verbatim; absolute-path targets are
     * resolved by fs_vfs_resolve). May be NULL. */
    int         (*symlink)(fs_vfs_node_t *parent, const char *name,
                           const char *target);
    /* WP-10-wp08fix1: read the target of a VFS_TYPE_SYMLINK node into
     * buf (NUL-terminated, at most cap-1 bytes). May be NULL. */
    int         (*readlink)(fs_vfs_node_t *node, char *buf, int cap);
} fs_vfs_dir_ops_t;

/* Registered file system type. */
struct fs_vfs_fs_type {
    char             name[16];
    fs_vfs_fs_ops_t    *fs_ops;
    fs_vfs_file_ops_t  *file_ops;
    fs_vfs_dir_ops_t   *dir_ops;
    /* BUG-0179 FIX (A12-014): per-fs name-matching rule. 1 = the fs's
     * lookup is case-insensitive (FAT32/exFAT); 0 = case-sensitive
     * (ramfs/ext4, the default for every registered fs). The VFS node
     * cache must match names the same way the backing fs does, or
     * resolution behaviour drifts with cache state ("/ETC" hitting the
     * cached "/etc" node on ramfs). Set via fs_vfs_set_fs_case_insensitive
     * after fs_vfs_register_fs. */
    int              case_insensitive;
};

/* A node in the global VFS tree. */
struct fs_vfs_node {
    char             name[VFS_NAME_LEN];
    int              type;             /* VFS_TYPE_* */
    u64              size;             /* file size in bytes (0 for dirs) */
    /* WP-10-wp08fix1: ownership + permission bits (see VFS_DEFAULT_*).
     * Kept on the VFS node so chmod/chown work for every mounted fs
     * while the node exists; ramfs also reports them via stat(). */
    u32              mode;
    u32              uid;
    u32              gid;
    u32              nlink;            /* hard link count (ramfs maintains) */
    /* BUG-0061: number of open fds referencing this node. unlink/rmdir
     * on a node with a non-zero count must not free its backing storage
     * (ramfs would leave a UAF window for the still-open fd; FAT32
     * would free the cluster chain and the open fd would resurrect it
     * over an unrelated file). */
    int              open_count;          /* fds currently open on node */
    fs_vfs_fs_type_t   *fs_type;          /* owning fs type */
    void            *private;          /* fs-specific data */
    fs_vfs_node_t      *parent;
    fs_vfs_node_t      *first_child;
    fs_vfs_node_t      *next_sibling;
};

/* An open file. */
struct fs_vfs_file {
    fs_vfs_node_t   *node;
    int           flags;
    u64           offset;
    int           in_use;
    /* BUG-0098 FIX (A2-5): reference count on the VFS-open-file slot.
     * One reference per kernel fd-table entry that points here
     * (sys_open = 1; sys_dup / sys_dup2 / fork inheritance each add
     * one via fs_vfs_fd_addref). fs_vfs_close only really closes the
     * underlying node when the LAST reference goes away; a dup'd or
     * fork-inherited fd therefore survives a sibling close instead of
     * silently dangling (writes to it failed or landed elsewhere). */
    int           refcnt;
};

/* Hook op codes (passed to hook callbacks). */
#define VFS_HOOK_OPEN    1
#define VFS_HOOK_CLOSE   2
#define VFS_HOOK_READ    3
#define VFS_HOOK_WRITE   4
#define VFS_HOOK_MOUNT   5
#define VFS_HOOK_UMOUNT  6

/* ---- API ---- */

void fs_vfs_init(void);

int fs_vfs_register_fs(const char *name,
                    fs_vfs_fs_ops_t *fs_ops,
                    fs_vfs_file_ops_t *file_ops,
                    fs_vfs_dir_ops_t *dir_ops);

/* BUG-0179 FIX (A12-014): declare a registered fs's name-matching rule.
 * flag 1 = case-insensitive lookups (FAT32/exFAT), 0 = case-sensitive
 * (ramfs/ext4). Applies to the registry entry (mount roots) and is read
 * by the VFS node-cache match. Returns 0 on success, -1 if `fs_name`
 * is not registered. */
int fs_vfs_set_fs_case_insensitive(const char *fs_name, int flag);

int fs_vfs_mount   (const char *fs_type, const char *mount_point, const char *device);
int fs_vfs_umount  (const char *mount_point);

int  fs_vfs_open   (const char *path, int flags);   /* returns fd >= 0 or negative err */
int  fs_vfs_read   (int fd, void *buf, int size);
int  fs_vfs_write  (int fd, const void *buf, int size);
i64  fs_vfs_seek   (int fd, i64 offset, int whence);   /* BUG-0183: full-width offsets */
int  fs_vfs_close  (int fd);
/* BUG-0098 FIX (A2-5): add one reference to an already-open VFS fd.
 * Called by sys_dup / sys_dup2 / fork for every inherited kind-1 fd so
 * that a later close by ONE holder only drops that holder's reference
 * instead of destroying the shared open file. Returns 0 or -1. */
int  fs_vfs_fd_addref (int fd);
int  fs_vfs_stat   (const char *path, fs_vfs_stat_t *st);
int  fs_vfs_mkdir  (const char *path);
int  fs_vfs_rmdir  (const char *path);
int  fs_vfs_readdir(const char *path, int index, fs_vfs_dirent_t *entry);
int  fs_vfs_unlink (const char *path);   /* WP-05 shell: delete a regular file */
int  fs_vfs_rename (const char *oldpath, const char *newpath);  /* within same fs */

/* ---- WP-10-wp08fix1: links + permissions ([category]_[specific]) ----
 * All operate on the resolved path; ramfs implements the backing ops.
 * Returns 0 on success, negative on error (-1 bad args / -2 unsupported
 * by the backing fs / -3 target missing / -4 name collision). */
int  fs_vfs_link   (const char *oldpath, const char *newpath);          /* hard link */
int  fs_vfs_symlink(const char *target,  const char *linkpath);         /* soft link */
int  fs_vfs_readlink(const char *path, char *buf, int cap);             /* read target */
int  fs_vfs_chmod  (const char *path, u32 mode);                        /* chmod */
int  fs_vfs_chown  (const char *path, u32 uid, u32 gid);                 /* chown */

fs_vfs_node_t *fs_vfs_resolve(const char *path);

int  fs_vfs_register_hook(int (*hook)(int op, const char *path));
void fs_vfs_list_mounts(void);

/* P2-06 FIX: expose the mount table so user-facing commands like `df`
 * don't have to hard-code strings. Each entry corresponds to one active
 * mount (mount_point + fs type name + device name). */
typedef struct {
    char mount_point[VFS_PATH_LEN];
    char fs_type[16];
    char device[VFS_NAME_LEN];
    int  in_use;
} fs_vfs_mount_info_t;

/* Fill `out[]` with up to `max` mount entries. Returns count of entries
 * written. */
int fs_vfs_get_mounts(fs_vfs_mount_info_t *out, int max);

/* Allocate a new VFS node (kmalloc'd, zeroed). */
fs_vfs_node_t *fs_vfs_alloc_node(const char *name, int type, fs_vfs_fs_type_t *fs_type);

/* Attach a child node to a parent (maintains sibling list). */
void fs_vfs_attach_child(fs_vfs_node_t *parent, fs_vfs_node_t *child);

/* Detach a child node from its parent. */
void fs_vfs_detach_child(fs_vfs_node_t *child);

#endif /* OC_VFS_H */
