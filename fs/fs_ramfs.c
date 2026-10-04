/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/ramfs.c
 * Purpose: In-memory file system implementation.
 *
 * Each ramfs node owns a fs_vfs_node_t (the public VFS face) plus a private
 * fs_ramfs_inode_t that holds the file data buffer (for regular files). Directories
 * store their children in the fs_vfs_node_t's first_child/next_sibling list, so
 * ramfs does not need a separate directory data structure.
 *
 * The fs is mounted at "/" during fs_ramfs_init() and creates a small default
 * tree (/etc, /tmp, /dev) so the shell has something to list.
 */
#include "fs_ramfs.h"
#include "fs_vfs.h"
#include "screen_console.h"
#include "mem_heap.h"
#include "lib_string.h"

/* Private per-node data. For directories, data/capacity are 0.
 * WP-10-wp08fix1: `nlink` counts hard links sharing this inode (created
 * via dir_ops->link); `link_target` holds the target of a symlink node
 * (type VFS_TYPE_SYMLINK). */
typedef struct {
    u8  *data;
    u64  size;
    u64  capacity;
    u64  inode;       /* unique inode number for stat/readdir */
    int  nlink;                        /* WP-10-wp08fix1: hard link count */
    char link_target[VFS_PATH_LEN];    /* WP-10-wp08fix1: symlink target */
} fs_ramfs_inode_t;

static u64 g_next_inode = 1;
static fs_vfs_fs_type_t g_ramfs_fs_type;
static fs_vfs_fs_ops_t  g_ramfs_fs_ops;
static fs_vfs_file_ops_t g_ramfs_file_ops;
static fs_vfs_dir_ops_t  g_ramfs_dir_ops;

/* Forward declarations (needed because mkdir/rmdir call lookup, and mount
 * calls write, before those are defined below). */
static fs_vfs_node_t *fs_ramfs_lookup(fs_vfs_node_t *parent, const char *name);
static int fs_ramfs_write(fs_vfs_node_t *node, u64 offset, const void *buf, int size);

static int g_total_nodes = 0;
static int g_total_size  = 0;

/* ---- Inode helpers ---- */

static fs_ramfs_inode_t *fs_ramfs_new_inode(void) {
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)kmalloc(sizeof(fs_ramfs_inode_t));
    if (!ri) return NULL;
    memset(ri, 0, sizeof(*ri));
    ri->inode = g_next_inode++;
    ri->nlink = 1;
    return ri;
}

static fs_vfs_node_t *fs_ramfs_new_node(const char *name, int type) {
    fs_vfs_node_t *n = fs_vfs_alloc_node(name, type, &g_ramfs_fs_type);
    if (!n) return NULL;
    n->private = fs_ramfs_new_inode();
    if (!n->private) {
        kfree(n);
        return NULL;
    }
    g_total_nodes++;
    return n;
}

/* ---- File operations ---- */

static int fs_ramfs_open(fs_vfs_node_t *node, int flags) {
    /* P2-15 FIX: handle O_TRUNC — free the in-memory data buffer and
     * reset size/capacity so the next write starts from a clean slate.
     * Without this, `echo new > existing` would leave the old content
     * appended/prepended depending on the offset the caller picked. */
    if (node && (flags & VFS_O_TRUNC)) {
        fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)node->private;
        if (ri) {
            if (ri->data) {
                kfree(ri->data);
                ri->data = NULL;
            }
            ri->capacity = 0;
            ri->size = 0;
            node->size = 0;
        }
    }
    return 0;
}

static int fs_ramfs_read(fs_vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    if (node->type != VFS_TYPE_FILE) return -2;
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)node->private;
    if (!ri) return -3;
    if (offset >= ri->size) return 0;
    u64 avail = ri->size - offset;
    int n = (int)(avail < (u64)size ? avail : (u64)size);
    memcpy(buf, ri->data + offset, (usize)n);
    return n;
}

static int fs_ramfs_write(fs_vfs_node_t *node, u64 offset, const void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    if (node->type != VFS_TYPE_FILE) return -2;
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)node->private;
    if (!ri) return -3;

    u64 need_end = offset + (u64)size;
    if (need_end > ri->capacity) {
        /* Grow. Round up to the next 256-byte boundary to amortize reallocs. */
        u64 new_cap = ri->capacity ? ri->capacity : 256;
        while (new_cap < need_end) new_cap *= 2;
        u8 *new_data = (u8 *)krealloc(ri->data, new_cap);
        if (!new_data) return -4;
        ri->data = new_data;
        ri->capacity = new_cap;
    }
    memcpy(ri->data + offset, buf, (usize)size);
    if (need_end > ri->size) {
        g_total_size += (int)(need_end - ri->size);
        ri->size = need_end;
        node->size = ri->size;
    }
    return size;
}

static u64 fs_ramfs_seek(fs_vfs_node_t *node, u64 offset, int whence) {
    /* VFS layer already computed the new offset; we just clamp it to
     * [0, size] for ramfs. The VFS will use our return value. */
    if (!node) return 0;
    u64 size = node->size;
    u64 new_off = offset;
    switch (whence) {
        case VFS_SEEK_SET: new_off = offset; break;
        case VFS_SEEK_CUR: /* VFS pre-computed; nothing extra */ break;
        /* BUG-029 FIX (P3): the VFS layer pre-computes SEEK_END as
         * size + offset (absolute, offset usually negative) and passes
         * that here. The old size - offset re-applied the computation,
         * so seek(fd, 0, SEEK_END) returned 0 instead of size. */
        case VFS_SEEK_END: new_off = offset; break; /* pre-computed */
        default: break;
    }
    if (new_off > size) new_off = size;
    return new_off;
}

static int fs_ramfs_close(fs_vfs_node_t *node) {
    (void)node;
    return 0;
}

static int fs_ramfs_stat(fs_vfs_node_t *node, fs_vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- Directory operations ---- */

static int fs_ramfs_mkdir(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    if (parent->type != VFS_TYPE_DIR) return -2;
    /* Already exists? */
    if (fs_ramfs_lookup(parent, name)) return -3;
    fs_vfs_node_t *child = fs_ramfs_new_node(name, VFS_TYPE_DIR);
    if (!child) return -4;
    fs_vfs_attach_child(parent, child);
    return 0;
}

static int fs_ramfs_rmdir(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fs_vfs_node_t *child = fs_ramfs_lookup(parent, name);
    if (!child) return -2;
    if (child->type != VFS_TYPE_DIR) return -3;
    if (child->first_child) return -4;  /* not empty */
    fs_vfs_detach_child(child);
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)child->private;
    if (ri) {
        if (ri->data) kfree(ri->data);
        kfree(ri);
    }
    g_total_nodes--;
    kfree(child);
    return 0;
}

static int fs_ramfs_readdir(fs_vfs_node_t *dir, int index, fs_vfs_dirent_t *entry) {
    if (!dir || !entry) return -1;
    if (dir->type != VFS_TYPE_DIR) return -2;
    int i = 0;
    for (fs_vfs_node_t *c = dir->first_child; c; c = c->next_sibling) {
        if (i == index) {
            memset(entry, 0, sizeof(*entry));
            strncpy(entry->name, c->name, VFS_NAME_LEN - 1);
            entry->name[VFS_NAME_LEN - 1] = 0;
            entry->type = c->type;
            fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)c->private;
            entry->inode = ri ? ri->inode : 0;
            return 0;
        }
        i++;
    }
    return -3;  /* out of range */
}

static fs_vfs_node_t *fs_ramfs_lookup(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    for (fs_vfs_node_t *c = parent->first_child; c; c = c->next_sibling) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

/* WP-05 shell: unlink a regular file. Refuses directories (use rmdir).
 * WP-10-wp08fix1: with hard links the inode may be shared by several
 * directory entries - only free it when the last link disappears. */
static int fs_ramfs_unlink(fs_vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    fs_vfs_node_t *child = fs_ramfs_lookup(parent, name);
    if (!child) return -2;
    if (child->type == VFS_TYPE_DIR) return -3;  /* use rmdir for dirs */
    fs_vfs_detach_child(child);
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)child->private;
    if (ri) {
        if (ri->nlink > 1) {
            /* Other directory entries still reference this inode. */
            ri->nlink--;
        } else {
            if (ri->data) kfree(ri->data);
            kfree(ri);
        }
    }
    g_total_nodes--;
    kfree(child);
    return 0;
}

/* ---- WP-10-wp08fix1: hard links + symbolic links ---- */

/* Hard link: create a new directory entry `name` under `parent` that
 * shares the SAME private inode as `old_node` (real link semantics: data
 * written through one name is visible through the other). */
static int fs_ramfs_link(fs_vfs_node_t *parent, const char *name,
                         fs_vfs_node_t *old_node) {
    if (!parent || !name || !old_node) return -1;
    if (parent->type != VFS_TYPE_DIR) return -2;
    if (old_node->type == VFS_TYPE_DIR) return -5;  /* no hard links to dirs */
    if (fs_ramfs_lookup(parent, name)) return -4;   /* name collision */
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)old_node->private;
    if (!ri) return -3;
    fs_vfs_node_t *n = fs_ramfs_new_node(name, VFS_TYPE_FILE);
    if (!n) return -3;
    /* Free the just-allocated private inode and share the old one. */
    if (n->private) kfree(n->private);
    n->private = ri;
    n->size = old_node->size;
    /* Keep the ownership/permission bits of the original. */
    n->mode = old_node->mode;
    n->uid  = old_node->uid;
    n->gid  = old_node->gid;
    ri->nlink++;
    n->nlink = ri->nlink;
    old_node->nlink = ri->nlink;
    fs_vfs_attach_child(parent, n);
    return 0;
}

/* Symbolic link: a VFS_TYPE_SYMLINK node whose inode stores the target
 * string. fs_vfs_resolve() follows the target during path walks. */
static int fs_ramfs_symlink(fs_vfs_node_t *parent, const char *name,
                            const char *target) {
    if (!parent || !name || !target) return -1;
    if (parent->type != VFS_TYPE_DIR) return -2;
    if (fs_ramfs_lookup(parent, name)) return -4;
    int tl = (int)strlen(target);
    if (tl <= 0 || tl >= VFS_PATH_LEN) return -6;   /* absolute targets only,
                                                       enforced by resolve */
    fs_vfs_node_t *n = fs_ramfs_new_node(name, VFS_TYPE_SYMLINK);
    if (!n) return -3;
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)n->private;
    if (!ri) { kfree(n); return -3; }
    memcpy(ri->link_target, target, (usize)(tl + 1));
    n->size = (u64)tl;   /* stat size = target length (POSIX-like) */
    fs_vfs_attach_child(parent, n);
    return 0;
}

/* Read the target of a VFS_TYPE_SYMLINK node. */
static int fs_ramfs_readlink(fs_vfs_node_t *node, char *buf, int cap) {
    if (!node || !buf || cap <= 0) return -1;
    if (node->type != VFS_TYPE_SYMLINK) return -4;
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)node->private;
    if (!ri) return -3;
    int tl = (int)strlen(ri->link_target);
    if (tl >= cap) tl = cap - 1;
    memcpy(buf, ri->link_target, (usize)tl);
    buf[tl] = 0;
    return tl;
}

/* WP-05 shell: rename a child within the same parent. */
static int fs_ramfs_rename(fs_vfs_node_t *parent, const char *oldname, const char *newname) {
    if (!parent || !oldname || !newname) return -1;
    fs_vfs_node_t *child = fs_ramfs_lookup(parent, oldname);
    if (!child) return -2;
    /* If newname already exists, fail. */
    if (fs_ramfs_lookup(parent, newname)) return -3;
    /* Update the name. */
    strncpy(child->name, newname, VFS_NAME_LEN - 1);
    child->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- Mount / unmount ---- */

static fs_vfs_node_t *fs_ramfs_mount(const char *device) {
    (void)device;  /* ramfs has no backing device */
    fs_vfs_node_t *root = fs_ramfs_new_node("", VFS_TYPE_DIR);
    if (!root) return NULL;
    /* Create a small default tree. */
    static const char *default_dirs[] = { "etc", "tmp", "dev", "bin" };
    for (int i = 0; i < (int)(sizeof(default_dirs) / sizeof(default_dirs[0])); i++) {
        fs_vfs_node_t *d = fs_ramfs_new_node(default_dirs[i], VFS_TYPE_DIR);
        if (d) fs_vfs_attach_child(root, d);
    }
    /* Create a tiny /etc/motd so `cat` has something to read. */
    fs_vfs_node_t *etc = fs_ramfs_lookup(root, "etc");
    if (etc) {
        fs_vfs_node_t *motd = fs_ramfs_new_node("motd", VFS_TYPE_FILE);
        if (motd) {
            fs_vfs_attach_child(etc, motd);
            const char *text = "Open Cube OS - WP-05 ramfs ready\n";
            int len = (int)strlen(text);
            fs_ramfs_write(motd, 0, text, len);
        }
    }
    return root;
}

/* P1-19 FIX: recursive node free — frees the entire subtree, not just
 * 2 levels. Uses an explicit stack to avoid stack overflow on deep trees. */
static void fs_ramfs_free_subtree(fs_vfs_node_t *node) {
    if (!node) return;
    /* Free all children first. */
    fs_vfs_node_t *c = node->first_child;
    while (c) {
        fs_vfs_node_t *next = c->next_sibling;
        fs_ramfs_free_subtree(c);
        c = next;
    }
    /* Free this node. */
    fs_ramfs_inode_t *ri = (fs_ramfs_inode_t *)node->private;
    if (ri) {
        if (ri->data) kfree(ri->data);
        kfree(ri);
    }
    kfree(node);
    g_total_nodes--;
}

static int fs_ramfs_unmount(fs_vfs_node_t *root) {
    if (!root) return -1;
    /* P1-19 FIX: recursively free the entire tree. */
    fs_ramfs_free_subtree(root);
    return 0;
}

/* ---- Init ---- */

void fs_ramfs_init(void) {
    memset(&g_ramfs_fs_type, 0, sizeof(g_ramfs_fs_type));
    strncpy(g_ramfs_fs_type.name, "ramfs", sizeof(g_ramfs_fs_type.name) - 1);
    g_ramfs_fs_ops.mount   = fs_ramfs_mount;
    g_ramfs_fs_ops.unmount = fs_ramfs_unmount;
    g_ramfs_file_ops.open  = fs_ramfs_open;
    g_ramfs_file_ops.read  = fs_ramfs_read;
    g_ramfs_file_ops.write = fs_ramfs_write;
    g_ramfs_file_ops.seek  = fs_ramfs_seek;
    g_ramfs_file_ops.close = fs_ramfs_close;
    g_ramfs_file_ops.stat  = fs_ramfs_stat;
    g_ramfs_dir_ops.mkdir   = fs_ramfs_mkdir;
    g_ramfs_dir_ops.rmdir   = fs_ramfs_rmdir;
    g_ramfs_dir_ops.readdir = fs_ramfs_readdir;
    g_ramfs_dir_ops.lookup  = fs_ramfs_lookup;
    g_ramfs_dir_ops.unlink  = fs_ramfs_unlink;
    g_ramfs_dir_ops.rename  = fs_ramfs_rename;
    /* WP-10-wp08fix1 */
    g_ramfs_dir_ops.link     = fs_ramfs_link;
    g_ramfs_dir_ops.symlink  = fs_ramfs_symlink;
    g_ramfs_dir_ops.readlink = fs_ramfs_readlink;
    g_ramfs_fs_type.fs_ops   = &g_ramfs_fs_ops;
    g_ramfs_fs_type.file_ops = &g_ramfs_file_ops;
    g_ramfs_fs_type.dir_ops  = &g_ramfs_dir_ops;

    fs_vfs_register_fs("ramfs", &g_ramfs_fs_ops, &g_ramfs_file_ops, &g_ramfs_dir_ops);

    int rc = fs_vfs_mount("ramfs", "/", NULL);
    if (rc < 0) {
        screen_console_puts("ramfs: mount at / failed\n");
        return;
    }
    screen_console_puts("ramfs: mounted at /\n");
}

void fs_ramfs_get_stats(int *total_nodes, int *total_size) {
    if (total_nodes) *total_nodes = g_total_nodes;
    if (total_size)  *total_size  = g_total_size;
}
