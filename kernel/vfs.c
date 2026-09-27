/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/vfs.c
 * Purpose: VFS abstraction layer implementation.
 *
 * Implements:
 *   - file system type table (max VFS_MAX_FS_TYPES)
 *   - mount table (max VFS_MAX_MOUNTS)
 *   - file descriptor table (max VFS_MAX_FDS)
 *   - path normalization (".", "..", leading "/", "//")
 *   - path resolution (walk components, cross mount points)
 *   - fd allocation and management
 */
#include "vfs.h"
#include "console.h"
#include "heap.h"
#include "string.h"

/* ---- Forward declarations ---- */
static vfs_node_t *vfs_resolve_parent_of(const char *path);

/* ---- Internal state ---- */

typedef struct {
    char            mount_point[VFS_PATH_LEN];  /* normalized, e.g. "/" or "/mnt" */
    vfs_fs_type_t  *fs_type;
    vfs_node_t     *root_node;                  /* root of mounted fs */
    char            device[VFS_NAME_LEN];
    int             in_use;
} vfs_mount_t;

static vfs_fs_type_t  g_fs_types[VFS_MAX_FS_TYPES];
static int            g_fs_type_count = 0;

static vfs_mount_t    g_mounts[VFS_MAX_MOUNTS];

static vfs_file_t     g_fds[VFS_MAX_FDS];

static vfs_node_t    *g_global_root = NULL;     /* the "/" node */

static int          (*g_hook)(int op, const char *path) = NULL;

/* ---- Helpers ---- */

static void vfs_invoke_hook(int op, const char *path) {
    if (g_hook) g_hook(op, path);
}

/* Find a registered fs type by name. */
static vfs_fs_type_t *vfs_find_fs_type(const char *name) {
    for (int i = 0; i < g_fs_type_count; i++) {
        if (oc_strcmp(g_fs_types[i].name, name) == 0) {
            return &g_fs_types[i];
        }
    }
    return NULL;
}

/* Find a mount table entry by normalized mount point path. */
static vfs_mount_t *vfs_find_mount(const char *mount_point) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].in_use &&
            oc_strcmp(g_mounts[i].mount_point, mount_point) == 0) {
            return &g_mounts[i];
        }
    }
    return NULL;
}

/* Allocate a file descriptor slot. Returns index >= 0 or -1 if full. */
static int vfs_alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!g_fds[i].in_use) {
            g_fds[i].in_use = 1;
            g_fds[i].node = NULL;
            g_fds[i].flags = 0;
            g_fds[i].offset = 0;
            return i;
        }
    }
    return -1;
}

/* ---- Node helpers ---- */

vfs_node_t *vfs_alloc_node(const char *name, int type, vfs_fs_type_t *fs_type) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return NULL;
    oc_memset(n, 0, sizeof(*n));
    if (name) {
        oc_strncpy(n->name, name, VFS_NAME_LEN - 1);
        n->name[VFS_NAME_LEN - 1] = 0;
    }
    n->type = type;
    n->fs_type = fs_type;
    return n;
}

void vfs_attach_child(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

void vfs_detach_child(vfs_node_t *child) {
    if (!child || !child->parent) return;
    vfs_node_t *p = child->parent;
    if (p->first_child == child) {
        p->first_child = child->next_sibling;
    } else {
        vfs_node_t *prev = p->first_child;
        while (prev && prev->next_sibling != child) prev = prev->next_sibling;
        if (prev) prev->next_sibling = child->next_sibling;
    }
    child->parent = NULL;
    child->next_sibling = NULL;
}

/* ---- Path normalization ----
 * Normalize an input path into `out` (size out_len). Handles:
 *   - leading "/" (absolute)
 *   - duplicate slashes "//"
 *   - "." components (dropped)
 *   - ".." components (pop, if possible)
 *   - trailing "/"
 * Returns 0 on success, -1 on overflow or non-absolute path (we require
 * absolute paths since the kernel has no cwd concept yet).
 */
static int vfs_normalize(const char *in, char *out, int out_len) {
    if (!in || !out || out_len < 2) return -1;
    if (in[0] != '/') return -1;            /* must be absolute */

    /* Component stack: we collect normalized components then join. */
    char comps[16][VFS_NAME_LEN];
    int ncomp = 0;

    int i = 0;
    while (in[i]) {
        /* Skip slashes. */
        while (in[i] == '/') i++;
        if (!in[i]) break;

        /* Read one component. */
        char comp[VFS_NAME_LEN];
        int ci = 0;
        while (in[i] && in[i] != '/' && ci < VFS_NAME_LEN - 1) {
            comp[ci++] = in[i++];
        }
        comp[ci] = 0;

        if (oc_strcmp(comp, ".") == 0) {
            /* skip */
        } else if (oc_strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if (ncomp >= 16) return -1;
            oc_strncpy(comps[ncomp], comp, VFS_NAME_LEN - 1);
            comps[ncomp][VFS_NAME_LEN - 1] = 0;
            ncomp++;
        }
    }

    /* Build output: always starts with "/". */
    int p = 0;
    out[p++] = '/';
    for (int c = 0; c < ncomp; c++) {
        int len = (int)oc_strlen(comps[c]);
        if (p + len + 1 >= out_len) return -1;
        oc_memcpy(out + p, comps[c], len);
        p += len;
        if (c < ncomp - 1) out[p++] = '/';
    }
    out[p] = 0;
    return 0;
}

/* ---- Init ---- */

void vfs_init(void) {
    /* Zero tables. */
    oc_memset(g_fs_types, 0, sizeof(g_fs_types));
    oc_memset(g_mounts, 0, sizeof(g_mounts));
    oc_memset(g_fds, 0, sizeof(g_fds));
    g_fs_type_count = 0;
    g_global_root = NULL;
    g_hook = NULL;

    /* Create the global root node. This is a placeholder dir until ramfs
     * mounts at "/". */
    g_global_root = vfs_alloc_node("", VFS_TYPE_DIR, NULL);
    if (!g_global_root) {
        oc_console_puts("vfs_init: failed to allocate global root\n");
        return;
    }
}

/* ---- FS type registration ---- */

int vfs_register_fs(const char *name,
                    vfs_fs_ops_t *fs_ops,
                    vfs_file_ops_t *file_ops,
                    vfs_dir_ops_t *dir_ops) {
    if (!name || (!fs_ops && !file_ops && !dir_ops)) return -1;
    /* Replace if already registered. */
    for (int i = 0; i < g_fs_type_count; i++) {
        if (oc_strcmp(g_fs_types[i].name, name) == 0) {
            g_fs_types[i].fs_ops = fs_ops;
            g_fs_types[i].file_ops = file_ops;
            g_fs_types[i].dir_ops = dir_ops;
            return 0;
        }
    }
    if (g_fs_type_count >= VFS_MAX_FS_TYPES) return -2;
    vfs_fs_type_t *ft = &g_fs_types[g_fs_type_count++];
    oc_memset(ft, 0, sizeof(*ft));
    oc_strncpy(ft->name, name, sizeof(ft->name) - 1);
    ft->name[sizeof(ft->name) - 1] = 0;
    ft->fs_ops = fs_ops;
    ft->file_ops = file_ops;
    ft->dir_ops = dir_ops;
    return 0;
}

/* ---- Mount / umount ---- */

int vfs_mount(const char *fs_type, const char *mount_point, const char *device) {
    if (!fs_type || !mount_point) return -1;

    char norm[VFS_PATH_LEN];
    if (vfs_normalize(mount_point, norm, sizeof(norm)) < 0) {
        oc_console_puts("vfs_mount: invalid path\n");
        return -1;
    }

    /* Already mounted? */
    if (vfs_find_mount(norm)) {
        oc_console_puts("vfs_mount: already mounted\n");
        return -2;
    }

    vfs_fs_type_t *ft = vfs_find_fs_type(fs_type);
    if (!ft) {
        oc_console_puts("vfs_mount: unknown fs type\n");
        return -3;
    }
    if (!ft->fs_ops || !ft->fs_ops->mount) {
        oc_console_puts("vfs_mount: fs has no mount op\n");
        return -4;
    }

    /* Find a free mount slot. */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        oc_console_puts("vfs_mount: mount table full\n");
        return -5;
    }

    /* Call fs-specific mount to get root node. */
    vfs_node_t *root = ft->fs_ops->mount(device);
    if (!root) {
        oc_console_puts("vfs_mount: fs mount failed\n");
        return -6;
    }
    root->fs_type = ft;

    /* Record the mount. */
    vfs_mount_t *m = &g_mounts[slot];
    oc_memset(m, 0, sizeof(*m));
    oc_strncpy(m->mount_point, norm, sizeof(m->mount_point) - 1);
    m->mount_point[sizeof(m->mount_point) - 1] = 0;
    m->fs_type = ft;
    m->root_node = root;
    if (device) {
        oc_strncpy(m->device, device, sizeof(m->device) - 1);
        m->device[sizeof(m->device) - 1] = 0;
    }
    m->in_use = 1;

    /* If mounting at "/", set global root to be this fs's root. */
    if (oc_strcmp(norm, "/") == 0) {
        /* Carry over any children of the placeholder global root (none,
         * typically) and substitute. */
        root->parent = NULL;
        g_global_root = root;
    } else {
        /* Mount at a sub-path: ensure parent exists in the existing tree,
         * create a placeholder dir node at the mount point, and link the
         * new root as a child. The fs root's parent points to the
         * placeholder so ".." works. */
        vfs_node_t *parent = vfs_resolve_parent_of(norm);
        if (!parent) {
            oc_console_puts("vfs_mount: parent path does not exist\n");
            ft->fs_ops->unmount(root);
            m->in_use = 0;
            return -7;
        }
        /* Extract the final component name. */
        char last[VFS_NAME_LEN];
        const char *slash = norm + oc_strlen(norm);
        while (slash > norm && *(slash - 1) != '/') slash--;
        int li = 0;
        while (*slash && li < VFS_NAME_LEN - 1) last[li++] = *slash++;
        last[li] = 0;

        oc_strncpy(root->name, last, VFS_NAME_LEN - 1);
        root->name[VFS_NAME_LEN - 1] = 0;
        root->parent = parent;
        /* Attach as child of parent. */
        root->next_sibling = parent->first_child;
        parent->first_child = root;
    }

    vfs_invoke_hook(VFS_HOOK_MOUNT, norm);

    {
        char msg[80];
        oc_strcpy(msg, "vfs: mounted ");
        oc_strcpy(msg + oc_strlen(msg), fs_type);
        oc_strcpy(msg + oc_strlen(msg), " at ");
        oc_strcpy(msg + oc_strlen(msg), norm);
        oc_strcpy(msg + oc_strlen(msg), "\n");
        oc_console_puts(msg);
    }
    return 0;
}

int vfs_umount(const char *mount_point) {
    if (!mount_point) return -1;
    char norm[VFS_PATH_LEN];
    if (vfs_normalize(mount_point, norm, sizeof(norm)) < 0) return -1;
    vfs_mount_t *m = vfs_find_mount(norm);
    if (!m) return -2;

    /* Close any fds pointing at nodes in this mount. */
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (g_fds[i].in_use && g_fds[i].node &&
            g_fds[i].node->fs_type == m->fs_type) {
            g_fds[i].in_use = 0;
            g_fds[i].node = NULL;
        }
    }

    /* Detach root from parent (if any). */
    if (m->root_node->parent) {
        vfs_detach_child(m->root_node);
    }

    /* Call fs unmount. */
    if (m->fs_type->fs_ops && m->fs_type->fs_ops->unmount) {
        m->fs_type->fs_ops->unmount(m->root_node);
    }

    vfs_invoke_hook(VFS_HOOK_UMOUNT, norm);
    m->in_use = 0;
    return 0;
}

/* ---- Path resolution ----
 * Resolve an absolute path to a vfs_node_t. Returns NULL if not found.
 *
 * Algorithm:
 *   1. Normalize the path.
 *   2. Find the longest matching mount point prefix.
 *   3. Start at that mount's root node.
 *   4. Walk the remaining components via dir_ops->lookup.
 */
vfs_node_t *vfs_resolve(const char *path) {
    if (!path) return NULL;
    char norm[VFS_PATH_LEN];
    if (vfs_normalize(path, norm, sizeof(norm)) < 0) return NULL;

    /* Find longest matching mount point. */
    int best = -1;
    int best_len = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) continue;
        int mpl = (int)oc_strlen(g_mounts[i].mount_point);
        /* A mount point "/" matches everything. */
        if (mpl == 1) {
            if (best < 0) { best = i; best_len = 1; }
            continue;
        }
        /* Otherwise require exact match or path/... */
        if (mpl > best_len &&
            oc_strncmp(norm, g_mounts[i].mount_point, mpl) == 0 &&
            (norm[mpl] == 0 || norm[mpl] == '/')) {
            best = i;
            best_len = mpl;
        }
    }
    if (best < 0) return NULL;

    vfs_node_t *cur = g_mounts[best].root_node;
    const char *rest;
    if (best_len == 1) {
        /* mount point is "/", skip the leading "/" */
        rest = norm + 1;
    } else {
        rest = norm + best_len;
        if (*rest == '/') rest++;
    }

    while (*rest) {
        /* Extract one component. */
        char comp[VFS_NAME_LEN];
        int ci = 0;
        while (*rest && *rest != '/' && ci < VFS_NAME_LEN - 1) {
            comp[ci++] = *rest++;
        }
        comp[ci] = 0;
        if (*rest == '/') rest++;
        if (ci == 0) continue;
        if (oc_strcmp(comp, ".") == 0) continue;
        if (oc_strcmp(comp, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        if (!cur->fs_type || !cur->fs_type->dir_ops ||
            !cur->fs_type->dir_ops->lookup) {
            return NULL;
        }
        /* P1-4 FIX: check if we already have a cached child with this name
         * before calling lookup. This prevents the node leak where every
         * lookup allocates a new node that is never freed. */
        vfs_node_t *cached = NULL;
        for (vfs_node_t *c = cur->first_child; c; c = c->next_sibling) {
            if (oc_strcasecmp(c->name, comp) == 0) {
                cached = c;
                break;
            }
        }
        if (cached) {
            cur = cached;
        } else {
            vfs_node_t *parent = cur;
            cur = parent->fs_type->dir_ops->lookup(parent, comp);
            if (!cur) return NULL;
            /* Attach the newly-looked-up node to its parent so future
             * resolves find it in the cache (prevents node leak). */
            cur->parent = parent;
            vfs_attach_child(parent, cur);
        }
    }
    return cur;
}

/* Resolve the parent directory of `path`. Returns NULL on failure.
 * Used by mount(), mkdir(), rmdir(), and O_CREAT open().
 * For "/a/b/c" returns the node for "/a/b". For "/a" returns the node
 * for "/". For "/" returns NULL (no parent). */
static vfs_node_t *vfs_resolve_parent_of(const char *path) {
    if (!path) return NULL;
    char norm[VFS_PATH_LEN];
    if (vfs_normalize(path, norm, sizeof(norm)) < 0) return NULL;
    int len = (int)oc_strlen(norm);
    if (len <= 1) return NULL;             /* parent of "/" doesn't exist */
    /* Strip trailing slashes (shouldn't be any after normalize, but be safe). */
    while (len > 1 && norm[len - 1] == '/') len--;
    /* Strip the last component up to (but not including) the slash that
     * precedes it. */
    while (len > 1 && norm[len - 1] != '/') len--;
    /* Now norm[0..len-1] is "/.../" with a trailing slash, or just "/".
     * For "/foo": after the loop len=1, norm[0]='/' -> parent is "/". */
    if (len <= 1) {
        norm[0] = '/';
        norm[1] = 0;
    } else {
        /* norm[len-1] is '/', keep it as the parent path terminator. */
        norm[len] = 0;
    }
    return vfs_resolve(norm);
}

/* Extract the final component (basename) of a normalized path. */
static int vfs_basename(const char *path, char *out, int out_len) {
    char norm[VFS_PATH_LEN];
    if (vfs_normalize(path, norm, sizeof(norm)) < 0) return -1;
    int len = (int)oc_strlen(norm);
    if (len <= 1) {  /* "/" */
        if (out_len < 2) return -1;
        out[0] = '/'; out[1] = 0;
        return 0;
    }
    const char *p = norm + len;
    while (p > norm && *(p - 1) != '/') p--;
    int i = 0;
    while (*p && i < out_len - 1) out[i++] = *p++;
    out[i] = 0;
    return 0;
}

/* ---- File operations ---- */

int vfs_open(const char *path, int flags) {
    if (!path) return -1;
    vfs_node_t *n = vfs_resolve(path);
    if (!n) {
        /* O_CREAT: ask the parent fs to create the file. We reuse the
         * dir_ops->mkdir entry point with a convention: if the name does
         * not end in '/', the fs implementation should create a regular
         * file. ramfs honors this; fat32 returns -1 (read-only). */
        if (flags & VFS_O_CREAT) {
            char base[VFS_NAME_LEN];
            if (vfs_basename(path, base, sizeof(base)) < 0) return -2;
            vfs_node_t *parent = vfs_resolve_parent_of(path);
            if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
                !parent->fs_type->dir_ops->lookup) {
                return -3;
            }
            /* P1-2 FIX: if the fs has a create function, use it (creates a
             * regular file with correct on-disk attributes). Otherwise fall
             * back to mkdir + type patch (ramfs style — works because ramfs
             * is in-memory; would corrupt on-disk fs like exFAT). */
            if (parent->fs_type->dir_ops->create) {
                int rc = parent->fs_type->dir_ops->create(parent, base);
                if (rc < 0) return -4;
            } else if (parent->fs_type->dir_ops->mkdir) {
                int rc = parent->fs_type->dir_ops->mkdir(parent, base);
                if (rc < 0) return -4;
            }
            n = parent->fs_type->dir_ops->lookup(parent, base);
            if (!n) return -5;
            /* Convert the just-created dir node into a regular file.
             * (Only needed for the mkdir fallback path; create path
             * already sets VFS_TYPE_FILE.) */
            n->type = VFS_TYPE_FILE;
            n->size = 0;
        } else {
            return -6;
        }
    }
    if (n->type != VFS_TYPE_FILE && n->type != VFS_TYPE_DEVICE) {
        return -7;  /* can't open a dir */
    }
    int fd = vfs_alloc_fd();
    if (fd < 0) return -8;
    g_fds[fd].node = n;
    g_fds[fd].flags = flags;
    g_fds[fd].offset = (flags & VFS_O_APPEND) ? n->size : 0;
    if (n->fs_type && n->fs_type->file_ops && n->fs_type->file_ops->open) {
        int rc = n->fs_type->file_ops->open(n, flags);
        if (rc < 0) {
            g_fds[fd].in_use = 0;
            return rc;
        }
    }
    vfs_invoke_hook(VFS_HOOK_OPEN, path);
    return fd;
}

int vfs_read(int fd, void *buf, int size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    vfs_file_t *f = &g_fds[fd];
    if (!f->node || !f->node->fs_type || !f->node->fs_type->file_ops ||
        !f->node->fs_type->file_ops->read) {
        return -2;
    }
    int n = f->node->fs_type->file_ops->read(f->node, f->offset, buf, size);
    if (n > 0) f->offset += (u64)n;
    return n;
}

int vfs_write(int fd, const void *buf, int size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    vfs_file_t *f = &g_fds[fd];
    if (!f->node || !f->node->fs_type || !f->node->fs_type->file_ops ||
        !f->node->fs_type->file_ops->write) {
        return -2;
    }
    if (f->flags & VFS_O_APPEND) {
        f->offset = f->node->size;
    }
    int n = f->node->fs_type->file_ops->write(f->node, f->offset, buf, size);
    if (n > 0) {
        f->offset += (u64)n;
        if (f->offset > f->node->size) f->node->size = f->offset;
    }
    return n;
}

int vfs_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    vfs_file_t *f = &g_fds[fd];
    if (!f->node) return -2;

    u64 new_off = f->offset;
    switch (whence) {
        case VFS_SEEK_SET: new_off = (u64)(i64)offset; break;
        case VFS_SEEK_CUR: new_off = (u64)((i64)f->offset + (i64)offset); break;
        case VFS_SEEK_END: new_off = (u64)((i64)f->node->size + (i64)offset); break;
        default: return -3;
    }
    /* Allow seeking past EOF (sparse files). Clamp negative results to 0. */
    if ((i64)new_off < 0) new_off = 0;
    f->offset = new_off;

    if (f->node->fs_type && f->node->fs_type->file_ops &&
        f->node->fs_type->file_ops->seek) {
        f->offset = f->node->fs_type->file_ops->seek(f->node, new_off, whence);
    }
    return (int)f->offset;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    vfs_file_t *f = &g_fds[fd];
    if (f->node && f->node->fs_type && f->node->fs_type->file_ops &&
        f->node->fs_type->file_ops->close) {
        f->node->fs_type->file_ops->close(f->node);
    }
    f->in_use = 0;
    f->node = NULL;
    f->flags = 0;
    f->offset = 0;
    vfs_invoke_hook(VFS_HOOK_CLOSE, "");
    return 0;
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return -1;
    vfs_node_t *n = vfs_resolve(path);
    if (!n) return -2;
    oc_memset(st, 0, sizeof(*st));
    st->type = n->type;
    st->size = n->size;
    oc_strncpy(st->name, n->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    if (n->fs_type && n->fs_type->file_ops && n->fs_type->file_ops->stat) {
        return n->fs_type->file_ops->stat(n, st);
    }
    return 0;
}

/* ---- Directory operations ---- */

int vfs_mkdir(const char *path) {
    if (!path) return -1;
    char base[VFS_NAME_LEN];
    if (vfs_basename(path, base, sizeof(base)) < 0) return -2;
    vfs_node_t *parent = vfs_resolve_parent_of(path);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
        !parent->fs_type->dir_ops->mkdir) {
        return -3;
    }
    return parent->fs_type->dir_ops->mkdir(parent, base);
}

int vfs_rmdir(const char *path) {
    if (!path) return -1;
    char base[VFS_NAME_LEN];
    if (vfs_basename(path, base, sizeof(base)) < 0) return -2;
    vfs_node_t *parent = vfs_resolve_parent_of(path);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
        !parent->fs_type->dir_ops->rmdir) {
        return -3;
    }
    return parent->fs_type->dir_ops->rmdir(parent, base);
}

int vfs_readdir(const char *path, int index, vfs_dirent_t *entry) {
    if (!path || !entry) return -1;
    vfs_node_t *dir = vfs_resolve(path);
    if (!dir || dir->type != VFS_TYPE_DIR) return -2;
    if (!dir->fs_type || !dir->fs_type->dir_ops ||
        !dir->fs_type->dir_ops->readdir) {
        return -3;
    }
    return dir->fs_type->dir_ops->readdir(dir, index, entry);
}

int vfs_unlink(const char *path) {
    if (!path) return -1;
    char base[VFS_NAME_LEN];
    if (vfs_basename(path, base, sizeof(base)) < 0) return -2;
    vfs_node_t *parent = vfs_resolve_parent_of(path);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops) return -3;
    /* If unlink is not implemented, fall back to rmdir (ramfs treats rmdir
     * on a file as an error, so this won't delete files - caller should
     * print "not supported"). */
    if (parent->fs_type->dir_ops->unlink) {
        return parent->fs_type->dir_ops->unlink(parent, base);
    }
    return -4;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    /* Resolve old parent + name. */
    char oldbase[VFS_NAME_LEN];
    if (vfs_basename(oldpath, oldbase, sizeof(oldbase)) < 0) return -2;
    vfs_node_t *oldparent = vfs_resolve_parent_of(oldpath);
    if (!oldparent) return -3;
    /* Resolve new parent + name. */
    char newbase[VFS_NAME_LEN];
    if (vfs_basename(newpath, newbase, sizeof(newbase)) < 0) return -4;
    vfs_node_t *newparent = vfs_resolve_parent_of(newpath);
    if (!newparent) return -5;
    /* Both must be on the same fs (same fs_type). */
    if (oldparent->fs_type != newparent->fs_type) return -6;
    if (!oldparent->fs_type || !oldparent->fs_type->dir_ops) return -7;
    /* If rename is supported, use it. */
    if (oldparent->fs_type->dir_ops->rename) {
        return oldparent->fs_type->dir_ops->rename(oldparent, oldbase, newbase);
    }
    return -8;
}

/* ---- Hooks ---- */

int vfs_register_hook(int (*hook)(int op, const char *path)) {
    if (g_hook) return -1;
    g_hook = hook;
    return 0;
}

/* ---- Diagnostics ---- */

void vfs_list_mounts(void) {
    oc_console_puts("VFS mount table:\n");
    int any = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) continue;
        any = 1;
        char line[VFS_PATH_LEN + 64];
        oc_strcpy(line, "  ");
        oc_strcpy(line + oc_strlen(line), g_mounts[i].mount_point);
        oc_strcpy(line + oc_strlen(line), "  ");
        oc_strcpy(line + oc_strlen(line), g_mounts[i].fs_type->name);
        if (g_mounts[i].device[0]) {
            oc_strcpy(line + oc_strlen(line), "  (dev=");
            oc_strcpy(line + oc_strlen(line), g_mounts[i].device);
            oc_strcpy(line + oc_strlen(line), ")");
        }
        oc_strcpy(line + oc_strlen(line), "\n");
        oc_console_puts(line);
    }
    if (!any) oc_console_puts("  (no mounts)\n");
}
