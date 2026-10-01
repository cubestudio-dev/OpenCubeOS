/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/ramfs.c
 * Purpose: In-memory file system implementation.
 *
 * Each ramfs node owns a vfs_node_t (the public VFS face) plus a private
 * ramfs_inode_t that holds the file data buffer (for regular files). Directories
 * store their children in the vfs_node_t's first_child/next_sibling list, so
 * ramfs does not need a separate directory data structure.
 *
 * The fs is mounted at "/" during ramfs_init() and creates a small default
 * tree (/etc, /tmp, /dev) so the shell has something to list.
 */
#include "ramfs.h"
#include "vfs.h"
#include "console.h"
#include "heap.h"
#include "string.h"

/* Private per-node data. For directories, data/capacity are 0. */
typedef struct {
    u8  *data;
    u64  size;
    u64  capacity;
    u64  inode;       /* unique inode number for stat/readdir */
} ramfs_inode_t;

static u64 g_next_inode = 1;
static vfs_fs_type_t g_ramfs_fs_type;
static vfs_fs_ops_t  g_ramfs_fs_ops;
static vfs_file_ops_t g_ramfs_file_ops;
static vfs_dir_ops_t  g_ramfs_dir_ops;

/* Forward declarations (needed because mkdir/rmdir call lookup, and mount
 * calls write, before those are defined below). */
static vfs_node_t *ramfs_lookup(vfs_node_t *parent, const char *name);
static int ramfs_write(vfs_node_t *node, u64 offset, const void *buf, int size);

static int g_total_nodes = 0;
static int g_total_size  = 0;

/* ---- Inode helpers ---- */

static ramfs_inode_t *ramfs_new_inode(void) {
    ramfs_inode_t *ri = (ramfs_inode_t *)kmalloc(sizeof(ramfs_inode_t));
    if (!ri) return NULL;
    oc_memset(ri, 0, sizeof(*ri));
    ri->inode = g_next_inode++;
    return ri;
}

static vfs_node_t *ramfs_new_node(const char *name, int type) {
    vfs_node_t *n = vfs_alloc_node(name, type, &g_ramfs_fs_type);
    if (!n) return NULL;
    n->private = ramfs_new_inode();
    if (!n->private) {
        kfree(n);
        return NULL;
    }
    g_total_nodes++;
    return n;
}

/* ---- File operations ---- */

static int ramfs_open(vfs_node_t *node, int flags) {
    /* P2-15 FIX: handle O_TRUNC — free the in-memory data buffer and
     * reset size/capacity so the next write starts from a clean slate.
     * Without this, `echo new > existing` would leave the old content
     * appended/prepended depending on the offset the caller picked. */
    if (node && (flags & VFS_O_TRUNC)) {
        ramfs_inode_t *ri = (ramfs_inode_t *)node->private;
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

static int ramfs_read(vfs_node_t *node, u64 offset, void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    if (node->type != VFS_TYPE_FILE) return -2;
    ramfs_inode_t *ri = (ramfs_inode_t *)node->private;
    if (!ri) return -3;
    if (offset >= ri->size) return 0;
    u64 avail = ri->size - offset;
    int n = (int)(avail < (u64)size ? avail : (u64)size);
    oc_memcpy(buf, ri->data + offset, (usize)n);
    return n;
}

static int ramfs_write(vfs_node_t *node, u64 offset, const void *buf, int size) {
    if (!node || !buf || size < 0) return -1;
    if (node->type != VFS_TYPE_FILE) return -2;
    ramfs_inode_t *ri = (ramfs_inode_t *)node->private;
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
    oc_memcpy(ri->data + offset, buf, (usize)size);
    if (need_end > ri->size) {
        g_total_size += (int)(need_end - ri->size);
        ri->size = need_end;
        node->size = ri->size;
    }
    return size;
}

static u64 ramfs_seek(vfs_node_t *node, u64 offset, int whence) {
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

static int ramfs_close(vfs_node_t *node) {
    (void)node;
    return 0;
}

static int ramfs_stat(vfs_node_t *node, vfs_stat_t *st) {
    if (!node || !st) return -1;
    st->type = node->type;
    st->size = node->size;
    oc_strncpy(st->name, node->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- Directory operations ---- */

static int ramfs_mkdir(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    if (parent->type != VFS_TYPE_DIR) return -2;
    /* Already exists? */
    if (ramfs_lookup(parent, name)) return -3;
    vfs_node_t *child = ramfs_new_node(name, VFS_TYPE_DIR);
    if (!child) return -4;
    vfs_attach_child(parent, child);
    return 0;
}

static int ramfs_rmdir(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    vfs_node_t *child = ramfs_lookup(parent, name);
    if (!child) return -2;
    if (child->type != VFS_TYPE_DIR) return -3;
    if (child->first_child) return -4;  /* not empty */
    vfs_detach_child(child);
    ramfs_inode_t *ri = (ramfs_inode_t *)child->private;
    if (ri) {
        if (ri->data) kfree(ri->data);
        kfree(ri);
    }
    g_total_nodes--;
    kfree(child);
    return 0;
}

static int ramfs_readdir(vfs_node_t *dir, int index, vfs_dirent_t *entry) {
    if (!dir || !entry) return -1;
    if (dir->type != VFS_TYPE_DIR) return -2;
    int i = 0;
    for (vfs_node_t *c = dir->first_child; c; c = c->next_sibling) {
        if (i == index) {
            oc_memset(entry, 0, sizeof(*entry));
            oc_strncpy(entry->name, c->name, VFS_NAME_LEN - 1);
            entry->name[VFS_NAME_LEN - 1] = 0;
            entry->type = c->type;
            ramfs_inode_t *ri = (ramfs_inode_t *)c->private;
            entry->inode = ri ? ri->inode : 0;
            return 0;
        }
        i++;
    }
    return -3;  /* out of range */
}

static vfs_node_t *ramfs_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    for (vfs_node_t *c = parent->first_child; c; c = c->next_sibling) {
        if (oc_strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

/* WP-05 shell: unlink a regular file. Refuses directories (use rmdir). */
static int ramfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return -1;
    vfs_node_t *child = ramfs_lookup(parent, name);
    if (!child) return -2;
    if (child->type == VFS_TYPE_DIR) return -3;  /* use rmdir for dirs */
    vfs_detach_child(child);
    ramfs_inode_t *ri = (ramfs_inode_t *)child->private;
    if (ri) {
        if (ri->data) kfree(ri->data);
        kfree(ri);
    }
    g_total_nodes--;
    kfree(child);
    return 0;
}

/* WP-05 shell: rename a child within the same parent. */
static int ramfs_rename(vfs_node_t *parent, const char *oldname, const char *newname) {
    if (!parent || !oldname || !newname) return -1;
    vfs_node_t *child = ramfs_lookup(parent, oldname);
    if (!child) return -2;
    /* If newname already exists, fail. */
    if (ramfs_lookup(parent, newname)) return -3;
    /* Update the name. */
    oc_strncpy(child->name, newname, VFS_NAME_LEN - 1);
    child->name[VFS_NAME_LEN - 1] = 0;
    return 0;
}

/* ---- Mount / unmount ---- */

static vfs_node_t *ramfs_mount(const char *device) {
    (void)device;  /* ramfs has no backing device */
    vfs_node_t *root = ramfs_new_node("", VFS_TYPE_DIR);
    if (!root) return NULL;
    /* Create a small default tree. */
    static const char *default_dirs[] = { "etc", "tmp", "dev", "bin" };
    for (int i = 0; i < (int)(sizeof(default_dirs) / sizeof(default_dirs[0])); i++) {
        vfs_node_t *d = ramfs_new_node(default_dirs[i], VFS_TYPE_DIR);
        if (d) vfs_attach_child(root, d);
    }
    /* Create a tiny /etc/motd so `cat` has something to read. */
    vfs_node_t *etc = ramfs_lookup(root, "etc");
    if (etc) {
        vfs_node_t *motd = ramfs_new_node("motd", VFS_TYPE_FILE);
        if (motd) {
            vfs_attach_child(etc, motd);
            const char *text = "Open Cube OS - WP-05 ramfs ready\n";
            int len = (int)oc_strlen(text);
            ramfs_write(motd, 0, text, len);
        }
    }
    return root;
}

/* P1-19 FIX: recursive node free — frees the entire subtree, not just
 * 2 levels. Uses an explicit stack to avoid stack overflow on deep trees. */
static void ramfs_free_subtree(vfs_node_t *node) {
    if (!node) return;
    /* Free all children first. */
    vfs_node_t *c = node->first_child;
    while (c) {
        vfs_node_t *next = c->next_sibling;
        ramfs_free_subtree(c);
        c = next;
    }
    /* Free this node. */
    ramfs_inode_t *ri = (ramfs_inode_t *)node->private;
    if (ri) {
        if (ri->data) kfree(ri->data);
        kfree(ri);
    }
    kfree(node);
    g_total_nodes--;
}

static int ramfs_unmount(vfs_node_t *root) {
    if (!root) return -1;
    /* P1-19 FIX: recursively free the entire tree. */
    ramfs_free_subtree(root);
    return 0;
}

/* ---- Init ---- */

void ramfs_init(void) {
    oc_memset(&g_ramfs_fs_type, 0, sizeof(g_ramfs_fs_type));
    oc_strncpy(g_ramfs_fs_type.name, "ramfs", sizeof(g_ramfs_fs_type.name) - 1);
    g_ramfs_fs_ops.mount   = ramfs_mount;
    g_ramfs_fs_ops.unmount = ramfs_unmount;
    g_ramfs_file_ops.open  = ramfs_open;
    g_ramfs_file_ops.read  = ramfs_read;
    g_ramfs_file_ops.write = ramfs_write;
    g_ramfs_file_ops.seek  = ramfs_seek;
    g_ramfs_file_ops.close = ramfs_close;
    g_ramfs_file_ops.stat  = ramfs_stat;
    g_ramfs_dir_ops.mkdir   = ramfs_mkdir;
    g_ramfs_dir_ops.rmdir   = ramfs_rmdir;
    g_ramfs_dir_ops.readdir = ramfs_readdir;
    g_ramfs_dir_ops.lookup  = ramfs_lookup;
    g_ramfs_dir_ops.unlink  = ramfs_unlink;
    g_ramfs_dir_ops.rename  = ramfs_rename;
    g_ramfs_fs_type.fs_ops   = &g_ramfs_fs_ops;
    g_ramfs_fs_type.file_ops = &g_ramfs_file_ops;
    g_ramfs_fs_type.dir_ops  = &g_ramfs_dir_ops;

    vfs_register_fs("ramfs", &g_ramfs_fs_ops, &g_ramfs_file_ops, &g_ramfs_dir_ops);

    int rc = vfs_mount("ramfs", "/", NULL);
    if (rc < 0) {
        oc_console_puts("ramfs: mount at / failed\n");
        return;
    }
    oc_console_puts("ramfs: mounted at /\n");
}

void ramfs_get_stats(int *total_nodes, int *total_size) {
    if (total_nodes) *total_nodes = g_total_nodes;
    if (total_size)  *total_size  = g_total_size;
}
