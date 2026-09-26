/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/vfs.h
 * Purpose: Virtual File System abstraction layer.
 *
 * The VFS provides a uniform API (open/read/write/seek/close/stat/mkdir/
 * rmdir/readdir/lookup) over multiple backing file systems. Each file system
 * type (ramfs, fat32, ...) registers a vfs_fs_type_t with mount/unmount hooks
 * and a set of file_ops/dir_ops. Paths are resolved by walking the global
 * node tree, transparently crossing mount points.
 *
 * Design notes:
 *   - vfs_node_t is the public face of every file/dir/device. It owns the
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

/* ---- Open flags (bitmask) ---- */
#define VFS_O_RDONLY  0x0001
#define VFS_O_WRONLY  0x0002
#define VFS_O_RDWR    0x0003   /* convenience: RDONLY | WRONLY == RDWR */
#define VFS_O_CREAT   0x0004
#define VFS_O_APPEND  0x0008

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
struct vfs_node;
struct vfs_file;
struct vfs_fs_type;

typedef struct vfs_node      vfs_node_t;
typedef struct vfs_file      vfs_file_t;
typedef struct vfs_fs_type   vfs_fs_type_t;

/* Result of stat() and readdir(). */
typedef struct {
    int  type;                          /* VFS_TYPE_* */
    u64  size;
    char name[VFS_NAME_LEN];
} vfs_stat_t;

typedef struct {
    char name[VFS_NAME_LEN];
    int  type;                          /* VFS_TYPE_* */
    u64  inode;                         /* fs-specific inode number (0 if N/A) */
} vfs_dirent_t;

/* File system operations: mount/unmount. */
typedef struct {
    vfs_node_t *(*mount)(const char *device);   /* returns root node or NULL */
    int         (*unmount)(vfs_node_t *root);   /* returns 0 on success */
} vfs_fs_ops_t;

/* File operations on a node. The VFS layer tracks the offset; read/write
 * receive it so the fs impl stays stateless per-open. */
typedef struct {
    int (*open )(vfs_node_t *node, int flags);
    int (*read )(vfs_node_t *node, u64 offset, void *buf, int size);
    int (*write)(vfs_node_t *node, u64 offset, const void *buf, int size);
    u64 (*seek )(vfs_node_t *node, u64 offset, int whence);   /* returns new offset */
    int (*close)(vfs_node_t *node);
    int (*stat )(vfs_node_t *node, vfs_stat_t *st);
} vfs_file_ops_t;

/* Directory operations. */
typedef struct {
    int         (*mkdir  )(vfs_node_t *parent, const char *name);
    int         (*rmdir  )(vfs_node_t *parent, const char *name);
    int         (*readdir)(vfs_node_t *dir, int index, vfs_dirent_t *entry);
    vfs_node_t *(*lookup )(vfs_node_t *parent, const char *name);
    /* WP-05 shell: unlink a regular file (remove from parent + free inode).
     * Returns 0 on success, negative on error. May be NULL (fs does not
     * support unlink, e.g. read-only FAT32). */
    int         (*unlink )(vfs_node_t *parent, const char *name);
    /* WP-05 shell: rename a child within the same parent. May be NULL (the
     * shell falls back to copy+unlink). Returns 0 on success. */
    int         (*rename )(vfs_node_t *parent, const char *oldname, const char *newname);
} vfs_dir_ops_t;

/* Registered file system type. */
struct vfs_fs_type {
    char             name[16];
    vfs_fs_ops_t    *fs_ops;
    vfs_file_ops_t  *file_ops;
    vfs_dir_ops_t   *dir_ops;
};

/* A node in the global VFS tree. */
struct vfs_node {
    char             name[VFS_NAME_LEN];
    int              type;             /* VFS_TYPE_* */
    u64              size;             /* file size in bytes (0 for dirs) */
    vfs_fs_type_t   *fs_type;          /* owning fs type */
    void            *private;          /* fs-specific data */
    vfs_node_t      *parent;
    vfs_node_t      *first_child;
    vfs_node_t      *next_sibling;
};

/* An open file. */
struct vfs_file {
    vfs_node_t   *node;
    int           flags;
    u64           offset;
    int           in_use;
};

/* Hook op codes (passed to hook callbacks). */
#define VFS_HOOK_OPEN    1
#define VFS_HOOK_CLOSE   2
#define VFS_HOOK_READ    3
#define VFS_HOOK_WRITE   4
#define VFS_HOOK_MOUNT   5
#define VFS_HOOK_UMOUNT  6

/* ---- API ---- */

void vfs_init(void);

int vfs_register_fs(const char *name,
                    vfs_fs_ops_t *fs_ops,
                    vfs_file_ops_t *file_ops,
                    vfs_dir_ops_t *dir_ops);

int vfs_mount   (const char *fs_type, const char *mount_point, const char *device);
int vfs_umount  (const char *mount_point);

int  vfs_open   (const char *path, int flags);   /* returns fd >= 0 or negative err */
int  vfs_read   (int fd, void *buf, int size);
int  vfs_write  (int fd, const void *buf, int size);
int  vfs_seek   (int fd, int offset, int whence);
int  vfs_close  (int fd);
int  vfs_stat   (const char *path, vfs_stat_t *st);
int  vfs_mkdir  (const char *path);
int  vfs_rmdir  (const char *path);
int  vfs_readdir(const char *path, int index, vfs_dirent_t *entry);
int  vfs_unlink (const char *path);   /* WP-05 shell: delete a regular file */
int  vfs_rename (const char *oldpath, const char *newpath);  /* within same fs */

vfs_node_t *vfs_resolve(const char *path);

int  vfs_register_hook(int (*hook)(int op, const char *path));
void vfs_list_mounts(void);

/* Allocate a new VFS node (kmalloc'd, zeroed). */
vfs_node_t *vfs_alloc_node(const char *name, int type, vfs_fs_type_t *fs_type);

/* Attach a child node to a parent (maintains sibling list). */
void vfs_attach_child(vfs_node_t *parent, vfs_node_t *child);

/* Detach a child node from its parent. */
void vfs_detach_child(vfs_node_t *child);

#endif /* OC_VFS_H */
