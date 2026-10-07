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
#include "fs_vfs.h"
#include "screen_console.h"
#include "mem_heap.h"
#include "lib_string.h"

/* ---- Forward declarations ---- */
static fs_vfs_node_t *fs_vfs_resolve_parent_of(const char *path);
static fs_vfs_node_t *fs_vfs_resolve_follow(const char *path, int depth);

/* ---- Internal state ---- */

typedef struct {
    char            mount_point[VFS_PATH_LEN];  /* normalized, e.g. "/" or "/mnt" */
    fs_vfs_fs_type_t  *fs_type;
    fs_vfs_node_t     *root_node;                  /* root of mounted fs */
    char            device[VFS_NAME_LEN];
    int             in_use;
    /* BUG-0184 FIX (A12-019 b): directory node that lived at the mount
     * point before the mount shadowed it. Kept alive here and restored
     * when the mount is removed, so mounting onto an existing directory
     * REUSES it instead of inserting a second same-name child (readdir
     * double-listing) and instead of leaking it. */
    fs_vfs_node_t     *shadowed_node;
} fs_vfs_mount_t;

static fs_vfs_fs_type_t  g_fs_types[VFS_MAX_FS_TYPES];
static int            g_fs_type_count = 0;

static fs_vfs_mount_t    g_mounts[VFS_MAX_MOUNTS];

static fs_vfs_file_t     g_fds[VFS_MAX_FDS];

static fs_vfs_node_t    *g_global_root = NULL;     /* the "/" node */

static int          (*g_hook)(int op, const char *path) = NULL;

/* ---- Helpers ---- */

static void fs_vfs_invoke_hook(int op, const char *path) {
    if (g_hook) g_hook(op, path);
}

/* Find a registered fs type by name. */
static fs_vfs_fs_type_t *fs_vfs_find_fs_type(const char *name) {
    for (int i = 0; i < g_fs_type_count; i++) {
        if (strcmp(g_fs_types[i].name, name) == 0) {
            return &g_fs_types[i];
        }
    }
    return NULL;
}

/* BUG-0179 FIX (A12-014): node-cache name match. The cache must apply
 * the SAME name rule as the backing fs's lookup op: FAT32/exFAT compare
 * case-insensitively, ramfs/ext4 compare exactly. The old code used
 * strcasecmp for every fs, so on ramfs "/ETC" hit the cached "/etc"
 * node even though fs_ramfs_lookup (strcmp) would have refused - the
 * resolution result depended on whether the node happened to be cached. */
static int fs_vfs_cache_name_match(const fs_vfs_node_t *parent,
                                   const char *child_name,
                                   const char *comp) {
    if (parent->fs_type && parent->fs_type->case_insensitive) {
        return strcasecmp(child_name, comp) == 0;
    }
    return strcmp(child_name, comp) == 0;
}

/* BUG-0179 FIX (A12-014): public setter, called by each fs's init right
 * after registration (the registry stores its own copy of the fs_type
 * descriptor, so the flag has to be recorded here as well as in the
 * fs's own static struct). */
int fs_vfs_set_fs_case_insensitive(const char *fs_name, int flag) {
    fs_vfs_fs_type_t *ft = fs_vfs_find_fs_type(fs_name);
    if (!ft) return -1;
    ft->case_insensitive = flag ? 1 : 0;
    return 0;
}

/* Find a mount table entry by normalized mount point path. */
static fs_vfs_mount_t *fs_vfs_find_mount(const char *mount_point) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].in_use &&
            strcmp(g_mounts[i].mount_point, mount_point) == 0) {
            return &g_mounts[i];
        }
    }
    return NULL;
}

/* Allocate a file descriptor slot. Returns index >= 0 or -1 if full. */
static int fs_vfs_alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!g_fds[i].in_use) {
            g_fds[i].in_use = 1;
            g_fds[i].node = NULL;
            g_fds[i].flags = 0;
            g_fds[i].offset = 0;
            g_fds[i].refcnt = 0;
            return i;
        }
    }
    return -1;
}

/* ---- Node helpers ---- */

fs_vfs_node_t *fs_vfs_alloc_node(const char *name, int type, fs_vfs_fs_type_t *fs_type) {
    fs_vfs_node_t *n = (fs_vfs_node_t *)kmalloc(sizeof(fs_vfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    if (name) {
        strncpy(n->name, name, VFS_NAME_LEN - 1);
        n->name[VFS_NAME_LEN - 1] = 0;
    }
    n->type = type;
    /* WP-10-wp08fix1: default ownership/permissions (chmod/chown change
     * these; stat reports them). */
    n->mode  = (type == VFS_TYPE_DIR) ? VFS_DEFAULT_DIR_MODE
                                      : VFS_DEFAULT_FILE_MODE;
    n->uid   = 0;
    n->gid   = 0;
    n->nlink = 1;
    n->fs_type = fs_type;
    return n;
}

void fs_vfs_attach_child(fs_vfs_node_t *parent, fs_vfs_node_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

void fs_vfs_detach_child(fs_vfs_node_t *child) {
    if (!child || !child->parent) return;
    fs_vfs_node_t *p = child->parent;
    if (p->first_child == child) {
        p->first_child = child->next_sibling;
    } else {
        fs_vfs_node_t *prev = p->first_child;
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
static int fs_vfs_normalize(const char *in, char *out, int out_len) {
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

        if (strcmp(comp, ".") == 0) {
            /* skip */
        } else if (strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if (ncomp >= 16) return -1;
            strncpy(comps[ncomp], comp, VFS_NAME_LEN - 1);
            comps[ncomp][VFS_NAME_LEN - 1] = 0;
            ncomp++;
        }
    }

    /* Build output: always starts with "/". */
    int p = 0;
    out[p++] = '/';
    for (int c = 0; c < ncomp; c++) {
        int len = (int)strlen(comps[c]);
        if (p + len + 1 >= out_len) return -1;
        memcpy(out + p, comps[c], len);
        p += len;
        if (c < ncomp - 1) out[p++] = '/';
    }
    out[p] = 0;
    return 0;
}

/* ---- Init ---- */

void fs_vfs_init(void) {
    /* Zero tables. */
    memset(g_fs_types, 0, sizeof(g_fs_types));
    memset(g_mounts, 0, sizeof(g_mounts));
    memset(g_fds, 0, sizeof(g_fds));
    g_fs_type_count = 0;
    g_global_root = NULL;
    g_hook = NULL;

    /* Create the global root node. This is a placeholder dir until ramfs
     * mounts at "/". */
    g_global_root = fs_vfs_alloc_node("", VFS_TYPE_DIR, NULL);
    if (!g_global_root) {
        screen_console_puts("vfs_init: failed to allocate global root\n");
        return;
    }
}

/* ---- FS type registration ---- */

int fs_vfs_register_fs(const char *name,
                    fs_vfs_fs_ops_t *fs_ops,
                    fs_vfs_file_ops_t *file_ops,
                    fs_vfs_dir_ops_t *dir_ops) {
    if (!name || (!fs_ops && !file_ops && !dir_ops)) return -1;
    /* Replace if already registered. */
    for (int i = 0; i < g_fs_type_count; i++) {
        if (strcmp(g_fs_types[i].name, name) == 0) {
            g_fs_types[i].fs_ops = fs_ops;
            g_fs_types[i].file_ops = file_ops;
            g_fs_types[i].dir_ops = dir_ops;
            return 0;
        }
    }
    if (g_fs_type_count >= VFS_MAX_FS_TYPES) return -2;
    fs_vfs_fs_type_t *ft = &g_fs_types[g_fs_type_count++];
    memset(ft, 0, sizeof(*ft));
    strncpy(ft->name, name, sizeof(ft->name) - 1);
    ft->name[sizeof(ft->name) - 1] = 0;
    ft->fs_ops = fs_ops;
    ft->file_ops = file_ops;
    ft->dir_ops = dir_ops;
    return 0;
}

/* ---- Mount / umount ---- */

int fs_vfs_mount(const char *fs_type, const char *mount_point, const char *device) {
    if (!fs_type || !mount_point) return -1;

    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(mount_point, norm, sizeof(norm)) < 0) {
        screen_console_puts("vfs_mount: invalid path\n");
        return -1;
    }

    /* Already mounted? */
    if (fs_vfs_find_mount(norm)) {
        screen_console_puts("vfs_mount: already mounted\n");
        return -2;
    }

    fs_vfs_fs_type_t *ft = fs_vfs_find_fs_type(fs_type);
    if (!ft) {
        screen_console_puts("vfs_mount: unknown fs type\n");
        return -3;
    }
    if (!ft->fs_ops || !ft->fs_ops->mount) {
        screen_console_puts("vfs_mount: fs has no mount op\n");
        return -4;
    }

    /* Find a free mount slot. */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        screen_console_puts("vfs_mount: mount table full\n");
        return -5;
    }

    /* Call fs-specific mount to get root node. */
    fs_vfs_node_t *root = ft->fs_ops->mount(device);
    if (!root) {
        screen_console_puts("vfs_mount: fs mount failed\n");
        return -6;
    }
    root->fs_type = ft;

    /* Record the mount. */
    fs_vfs_mount_t *m = &g_mounts[slot];
    memset(m, 0, sizeof(*m));
    strncpy(m->mount_point, norm, sizeof(m->mount_point) - 1);
    m->mount_point[sizeof(m->mount_point) - 1] = 0;
    m->fs_type = ft;
    m->root_node = root;
    if (device) {
        strncpy(m->device, device, sizeof(m->device) - 1);
        m->device[sizeof(m->device) - 1] = 0;
    }
    m->in_use = 1;

    /* If mounting at "/", set global root to be this fs's root. */
    if (strcmp(norm, "/") == 0) {
        /* BUG-0184 FIX (A12-019 a): the placeholder root allocated by
         * fs_vfs_init() used to be silently dropped here (leaked), and
         * any children attached to it before the first mount were lost.
         * Carry the children over to the real root, then release the
         * placeholder. The umount side refuses "/" (BUG-0133), so
         * g_global_root can never dangle and this substitution happens
         * exactly once. */
        if (g_global_root && g_global_root != root) {
            fs_vfs_node_t *c = g_global_root->first_child;
            while (c) {
                fs_vfs_node_t *next = c->next_sibling;
                fs_vfs_attach_child(root, c);
                c = next;
            }
            g_global_root->first_child = NULL;
            /* Placeholder nodes carry no fs private data (fs_type NULL,
             * private NULL from fs_vfs_alloc_node), so a plain kfree is
             * complete. */
            kfree(g_global_root);
        }
        root->parent = NULL;
        g_global_root = root;
    } else {
        /* Mount at a sub-path: ensure parent exists in the existing tree,
         * create a placeholder dir node at the mount point, and link the
         * new root as a child. The fs root's parent points to the
         * placeholder so ".." works. */
        fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(norm);
        if (!parent) {
            screen_console_puts("vfs_mount: parent path does not exist\n");
            ft->fs_ops->unmount(root);
            m->in_use = 0;
            return -7;
        }
        /* Extract the final component name. */
        char last[VFS_NAME_LEN];
        const char *slash = norm + strlen(norm);
        while (slash > norm && *(slash - 1) != '/') slash--;
        int li = 0;
        while (*slash && li < VFS_NAME_LEN - 1) last[li++] = *slash++;
        last[li] = 0;

        strncpy(root->name, last, VFS_NAME_LEN - 1);
        root->name[VFS_NAME_LEN - 1] = 0;
        root->parent = parent;
        /* BUG-0184 FIX (A12-019 b): mount-shadowing. The old code blindly
         * linked a SECOND child with the same name, so readdir listed the
         * mount point twice and the tree disagreed with the mount table.
         * A second mount at the same exact path is still refused (see the
         * fs_vfs_find_mount check above); mounting onto an existing plain
         * directory now SHADOWS it Linux-style: the old node is detached
         * from the sibling list, kept in the mount entry, and restored on
         * umount. One visible entry, existing node reused, nothing freed
         * while hidden (its fds and cached children stay valid). */
        fs_vfs_node_t *shadowed = NULL;
        for (fs_vfs_node_t *c = parent->first_child; c; c = c->next_sibling) {
            if (strcmp(c->name, last) == 0 && c != root) {
                if (c->type != VFS_TYPE_DIR) {
                    screen_console_puts("vfs_mount: mount point exists and is not a directory\n");
                    ft->fs_ops->unmount(root);
                    m->in_use = 0;
                    return -8;
                }
                shadowed = c;
                break;
            }
        }
        if (shadowed) {
            fs_vfs_detach_child(shadowed);
            m->shadowed_node = shadowed;
            screen_console_puts("vfs: shadow mount (previous directory restored on umount)\n");
        }
        /* Attach as child of parent. */
        root->next_sibling = parent->first_child;
        parent->first_child = root;
    }

    fs_vfs_invoke_hook(VFS_HOOK_MOUNT, norm);

    {
        /* P0fix1 BUG-0001 (A12-001): the mount message used to be built
         * with unbounded strcpy() into msg[80] while "vfs: mounted " +
         * fs_type(<=15) + " at " + norm(<=255) + "\n" can reach ~289
         * bytes -> stack overflow. Build the message with explicit
         * bounds and truncate the (display-only) path instead. */
        char msg[80];
        int flen = (int)strlen(fs_type);
        int nlen = (int)strlen(norm);
        if (flen > 15) flen = 15;
        int max_norm = (int)sizeof(msg) - 1 - 13 - flen - 4 - 1;
        if (max_norm < 0) max_norm = 0;
        if (nlen > max_norm) nlen = max_norm;
        int mp = 0;
        memcpy(msg + mp, "vfs: mounted ", 13); mp += 13;
        memcpy(msg + mp, fs_type, flen); mp += flen;
        memcpy(msg + mp, " at ", 4); mp += 4;
        memcpy(msg + mp, norm, nlen); mp += nlen;
        msg[mp++] = '\n';
        msg[mp] = 0;
        screen_console_puts(msg);
    }
    return 0;
}

/* WP-09-FIX BUG-017: does `n` belong to the cached subtree rooted at
 * `root`? Used to close only the fds that really belong to the mount
 * being unmounted. The old check compared fs_type, which closed every
 * fd of the same filesystem TYPE (two FAT32 mounts: unmounting one
 * killed the other's open files). */
static int fs_vfs_node_in_subtree(fs_vfs_node_t *root, fs_vfs_node_t *n) {
    if (!root || !n) return 0;
    if (n == root) return 1;
    for (fs_vfs_node_t *c = root->first_child; c; c = c->next_sibling) {
        if (fs_vfs_node_in_subtree(c, n)) return 1;
    }
    return 0;
}

int fs_vfs_umount(const char *mount_point) {
    if (!mount_point) return -1;
    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(mount_point, norm, sizeof(norm)) < 0) return -1;
    /* BUG-0133 FIX: the root of the VFS namespace (the root ramfs) is the
     * backbone every other path resolves through. Unmounting it used to
     * succeed, blank the mount table, drop every ramfs file (including
     * /etc) and leave the system with no recovery path but a reboot.
     * Refuse explicitly. */
    if (strcmp(norm, "/") == 0) {
        screen_console_puts("vfs: cannot unmount the root filesystem\n");
        return -4;
    }
    /* P0fix1 BUG-0002 (A12-002, trigger surface 2): umounting "/" (the
     * root ramfs) used to release the whole tree INCLUDING nested mount
     * roots (their ->private is another fs's inode/context), leaving
     * those mount table entries dangling -> UAF. Nested mounts must be
     * removed first; refuse while children are still mounted. */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) continue;
        int mpl = (int)strlen(g_mounts[i].mount_point);
        int nml = (int)strlen(norm);
        if (mpl > nml && strncmp(g_mounts[i].mount_point, norm, nml) == 0 &&
            g_mounts[i].mount_point[nml] == '/') {
            screen_console_puts("vfs: umount nested mounts first\n");
            return -3;
        }
    }
    fs_vfs_mount_t *m = fs_vfs_find_mount(norm);
    if (!m) return -2;

    /* BUG-0133 FIX: refuse when the mount is busy instead of silently
     * stealing open file descriptors. The old code zeroed any fd whose
     * node lived inside this mount and detached it from its owner, so a
     * writer kept a fd number pointing at nothing. Standard behavior is
     * EBUSY: the caller closes its files and retries. */
    int busy = 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (g_fds[i].in_use && g_fds[i].node &&
            fs_vfs_node_in_subtree(m->root_node, g_fds[i].node)) {
            busy++;
        }
    }
    if (busy > 0) {
        screen_console_puts("vfs: umount target is busy (");
        char nb[12];
        u64_to_str((u64)busy, nb);
        screen_console_puts(nb);
        screen_console_puts(" open file(s))\n");
        return -5;
    }

    /* Detach root from parent (if any) and restore any shadowed node. */
    if (m->root_node->parent) {
        fs_vfs_node_t *mparent = m->root_node->parent;
        fs_vfs_detach_child(m->root_node);
        /* BUG-0184 FIX (A12-019 b): bring back the directory the mount
         * shadowed, so the tree looks exactly like it did before the
         * mount (its cached children and open fds were kept alive). */
        if (m->shadowed_node) {
            fs_vfs_attach_child(mparent, m->shadowed_node);
            m->shadowed_node = NULL;
        }
    }

    /* Call fs unmount. */
    if (m->fs_type->fs_ops && m->fs_type->fs_ops->unmount) {
        m->fs_type->fs_ops->unmount(m->root_node);
    }

    fs_vfs_invoke_hook(VFS_HOOK_UMOUNT, norm);
    m->in_use = 0;
    return 0;
}

/* ---- Path resolution ----
 * Resolve an absolute path to a fs_vfs_node_t. Returns NULL if not found.
 *
 * Algorithm:
 *   1. Normalize the path.
 *   2. Find the longest matching mount point prefix.
 *   3. Start at that mount's root node.
 *   4. Walk the remaining components via dir_ops->lookup.
 */
fs_vfs_node_t *fs_vfs_resolve(const char *path) {
    return fs_vfs_resolve_follow(path, 0);
}

/* WP-10-wp08fix1: resolve with symlink following. When any path component
 * resolves to a VFS_TYPE_SYMLINK node, the link target is spliced in front
 * of the not-yet-walked components and resolution restarts (up to 8 hops,
 * mirroring the classic ELOOP limit). Only absolute targets are supported
 * (the ush `ln -s` documents this). */
static fs_vfs_node_t *fs_vfs_resolve_follow(const char *path, int depth) {
    if (!path) return NULL;
    if (depth > 8) return NULL;              /* symlink loop */
    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(path, norm, sizeof(norm)) < 0) return NULL;

    /* Find longest matching mount point. */
    int best = -1;
    int best_len = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) continue;
        int mpl = (int)strlen(g_mounts[i].mount_point);
        /* A mount point "/" matches everything. */
        if (mpl == 1) {
            if (best < 0) { best = i; best_len = 1; }
            continue;
        }
        /* Otherwise require exact match or path/... */
        if (mpl > best_len &&
            strncmp(norm, g_mounts[i].mount_point, mpl) == 0 &&
            (norm[mpl] == 0 || norm[mpl] == '/')) {
            best = i;
            best_len = mpl;
        }
    }
    if (best < 0) return NULL;

    fs_vfs_node_t *cur = g_mounts[best].root_node;
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
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
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
        /* BUG-0179 FIX (A12-014): match with the backing fs's own rule
         * (FAT32/exFAT insensitive, ramfs/ext4 exact) instead of a
         * blanket strcasecmp. */
        fs_vfs_node_t *cached = NULL;
        for (fs_vfs_node_t *c = cur->first_child; c; c = c->next_sibling) {
            if (fs_vfs_cache_name_match(cur, c->name, comp)) {
                cached = c;
                break;
            }
        }
        if (cached) {
            cur = cached;
        } else {
            fs_vfs_node_t *parent = cur;
            cur = parent->fs_type->dir_ops->lookup(parent, comp);
            if (!cur) return NULL;
            /* Attach the newly-looked-up node to its parent so future
             * resolves find it in the cache (prevents node leak). */
            cur->parent = parent;
            fs_vfs_attach_child(parent, cur);
        }
        /* WP-10-wp08fix1: follow a symlink component. Splice the target
         * in front of the remaining components and restart the walk. */
        if (cur->type == VFS_TYPE_SYMLINK) {
            if (!cur->fs_type || !cur->fs_type->dir_ops ||
                !cur->fs_type->dir_ops->readlink) {
                return NULL;
            }
            char target[VFS_PATH_LEN];
            if (cur->fs_type->dir_ops->readlink(cur, target,
                                                sizeof(target)) < 0) {
                return NULL;
            }
            if (target[0] != '/') return NULL;   /* absolute targets only */
            char joined[VFS_PATH_LEN];
            int tl = (int)strlen(target);
            if (tl <= 0 || tl >= VFS_PATH_LEN - 2) return NULL;
            memcpy(joined, target, (usize)(tl + 1));
            if (*rest) {
                int jl = tl;
                if (jl > 0 && joined[jl - 1] != '/' ) {
                    joined[jl++] = '/';
                }
                int rl = (int)strlen(rest);
                if (jl + rl >= VFS_PATH_LEN) return NULL;
                memcpy(joined + jl, rest, (usize)(rl + 1));
            }
            return fs_vfs_resolve_follow(joined, depth + 1);
        }
    }
    return cur;
}

/* Resolve the parent directory of `path`. Returns NULL on failure.
 * Used by mount(), mkdir(), rmdir(), and O_CREAT open().
 * For "/a/b/c" returns the node for "/a/b". For "/a" returns the node
 * for "/". For "/" returns NULL (no parent). */
static fs_vfs_node_t *fs_vfs_resolve_parent_of(const char *path) {
    if (!path) return NULL;
    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(path, norm, sizeof(norm)) < 0) return NULL;
    int len = (int)strlen(norm);
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
    return fs_vfs_resolve(norm);
}

/* Extract the final component (basename) of a normalized path. */
static int fs_vfs_basename(const char *path, char *out, int out_len) {
    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(path, norm, sizeof(norm)) < 0) return -1;
    int len = (int)strlen(norm);
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

/* BUG-0182 FIX (A12-017): the permission bits were write-only.
 * chmod/chown stored them, but no open/read/write path ever consulted
 * them, so a mode-0000 file was readable/writable by anyone.
 *
 * Enforcement point (single-user kernel): the kernel has no per-task
 * uid - every task executes as the implicit owner identity uid 0. A
 * node owned by uid 0 is therefore checked against the OWNER class
 * bits; a node chown'd to another uid is checked against the OTHER
 * class bits (we are not the owner, and there are no groups). Default
 * node modes are 0644 (file) / 0755 (dir), so every pre-existing
 * workflow (cat/echo/cp/editor on default files) keeps working; mode
 * 0000 refuses both directions. */
static int fs_vfs_node_grants(const fs_vfs_node_t *n, int want_write) {
    u32 mode = n->mode & 0777u;            /* rwxrwxrwx */
    u32 cls = (n->uid == 0) ? ((mode >> 6) & 7u)   /* owner class  */
                            : (mode & 7u);         /* other class  */
    return (cls & (want_write ? 2u : 4u)) != 0;
}

/* ---- File operations ---- */

int fs_vfs_open(const char *path, int flags) {
    if (!path) return -1;
    fs_vfs_node_t *n = fs_vfs_resolve(path);
    if (!n) {
        /* O_CREAT: ask the parent fs to create the file. We reuse the
         * dir_ops->mkdir entry point with a convention: if the name does
         * not end in '/', the fs implementation should create a regular
         * file. ramfs honors this; fat32 returns -1 (read-only). */
        if (flags & VFS_O_CREAT) {
            char base[VFS_NAME_LEN];
            if (fs_vfs_basename(path, base, sizeof(base)) < 0) return -2;
            fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(path);
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
    /* BUG-0182 FIX (A12-017): open() is the permission gate - the mode
     * is consulted here (see fs_vfs_node_grants) and again defensively
     * on every read()/write() through the fd below. POSIX semantics:
     * O_RDONLY == 0, so "not explicitly write-only" counts as a read
     * open (the syscall layer passes user flags through verbatim and
     * user programs open read-only with flags 0). */
    {
        int want_read  = (flags & VFS_O_WRONLY) == 0;
        int want_write = (flags & VFS_O_WRONLY) != 0;   /* RDWR = RDONLY|WRONLY */
        if ((want_read && !fs_vfs_node_grants(n, 0)) ||
            (want_write && !fs_vfs_node_grants(n, 1))) {
            return -9;   /* EACCES */
        }
    }
    int fd = fs_vfs_alloc_fd();
    if (fd < 0) return -8;
    g_fds[fd].node = n;
    g_fds[fd].flags = flags;
    /* P2-15 FIX: when reopening an existing file with O_TRUNC and the
     * file system has no fs-specific truncate op, zero the in-memory
     * size here as a defensive fallback. fs-specific truncation (freeing
     * data buffers / disk clusters) is done by the file_ops->open
     * callback below. */
    if (flags & VFS_O_TRUNC) {
        n->size = 0;
    }
    g_fds[fd].offset = (flags & VFS_O_APPEND) ? n->size : 0;
    /* BUG-0098 FIX (A2-5): a fresh open holds exactly one reference. */
    g_fds[fd].refcnt = 1;
    if (n->fs_type && n->fs_type->file_ops && n->fs_type->file_ops->open) {
        int rc = n->fs_type->file_ops->open(n, flags);
        if (rc < 0) {
            g_fds[fd].in_use = 0;
            g_fds[fd].refcnt = 0;
            g_fds[fd].node = NULL;
            return rc;
        }
    }
    /* BUG-0061: track the open count so unlink/rmdir can refuse to free
     * backing storage that is still in use. */
    n->open_count++;
    fs_vfs_invoke_hook(VFS_HOOK_OPEN, path);
    return fd;
}

int fs_vfs_read(int fd, void *buf, int size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    fs_vfs_file_t *f = &g_fds[fd];
    if (!f->node) return -2;
    /* BUG-0178 FIX (A12-013), read side: refuse reads only on an
     * UNAMBIGUOUS write-only open (WRONLY set while RDONLY and CREAT
     * are clear). The syscall layer passes user flags through verbatim
     * and shipped user programs encode read intent both as 0 (POSIX
     * O_RDONLY: sort/uniq) and as WRONLY|CREAT (6: wc/uniq open with
     * create, then read), so a strict "RDONLY bit required" gate
     * regressed those existing workflows. A plain VFS_O_WRONLY fd
     * (flags == 0x2 exactly) is still refused. The WRITE side below is
     * the side this bug is about and stays strict. */
    if ((f->flags & VFS_O_WRONLY) != 0 &&
        (f->flags & (VFS_O_RDONLY | VFS_O_CREAT)) == 0) return -1;
    /* BUG-0182 FIX (A12-017): re-check the permission bits at use time
     * (a chmod after open revokes access on the next call). */
    if (!fs_vfs_node_grants(f->node, 0)) return -9;
    if (!f->node->fs_type || !f->node->fs_type->file_ops ||
        !f->node->fs_type->file_ops->read) {
        return -2;
    }
    int n = f->node->fs_type->file_ops->read(f->node, f->offset, buf, size);
    if (n > 0) f->offset += (u64)n;
    return n;
}

/* P2-46 FIX: Check if the fd is open for writing before writing. */
int fs_vfs_write(int fd, const void *buf, int size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    fs_vfs_file_t *f = &g_fds[fd];
    /* BUG-0178 FIX (A12-013): the old equality test `f->flags ==
     * VFS_O_RDONLY` only caught the exact value 0x1, so O_RDONLY|O_APPEND
     * (0x9), O_RDONLY|O_CREAT (0x5) or flags==0 all slipped through and
     * the write went ahead on a read-only fd. Bitwise rule instead: a fd
     * has write access only when the WRONLY bit is set (which includes
     * both halves of O_RDWR = RDONLY|WRONLY). Dual-sided: O_RDONLY and
     * O_RDONLY|O_APPEND refuse, O_RDWR and O_WRONLY still write. */
    if ((f->flags & VFS_O_WRONLY) == 0) return -1;   /* not opened for writing */
    if (!f->node) return -2;
    /* BUG-0182 FIX (A12-017): re-check the permission bits at use time. */
    if (!fs_vfs_node_grants(f->node, 1)) return -9;
    if (!f->node->fs_type || !f->node->fs_type->file_ops ||
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

i64 fs_vfs_seek(int fd, i64 offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    fs_vfs_file_t *f = &g_fds[fd];
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
        /* WP-09-FIX BUG-029: pass SEEK_SET — new_off is already an
         * absolute offset (VFS computed SET/CUR/END above). The old code
         * re-passed the original whence, so ramfs/fat32 applied their
         * size-offset END formula AGAIN on the absolute value, making
         * SEEK_END collapse to 0. */
        f->offset = f->node->fs_type->file_ops->seek(f->node, new_off, VFS_SEEK_SET);
    }
    /* BUG-0183 FIX (A12-018): the offset used to be narrowed with
     * (int), so any seek landing beyond 2 GiB came back as a negative
     * number and every caller read it as a failure. VFS offsets are
     * u64; return them at full width (the prototype changed from int
     * to i64; the sole caller chain is internal). */
    return (i64)f->offset;
}

int fs_vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    fs_vfs_file_t *f = &g_fds[fd];
    /* BUG-0098 FIX (A2-5): shared open-file slots (dup / dup2 / fork
     * inheritance) are reference-counted. A close from ONE holder only
     * drops that holder's reference; the underlying node close (and the
     * fs-specific close callback + node open_count decrement) runs when
     * the LAST reference goes away. Without this, closing one dup'd fd
     * destroyed the slot for every other holder: their later reads
     * returned -1 and writes silently failed or were lost. */
    if (f->refcnt > 1) {
        f->refcnt--;
        return 0;
    }
    if (f->node && f->node->fs_type && f->node->fs_type->file_ops &&
        f->node->fs_type->file_ops->close) {
        f->node->fs_type->file_ops->close(f->node);
    }
    /* BUG-0061: matching decrement for the open_count in fs_vfs_open. */
    if (f->node && f->node->open_count > 0) f->node->open_count--;
    f->in_use = 0;
    f->node = NULL;
    f->flags = 0;
    f->offset = 0;
    f->refcnt = 0;
    fs_vfs_invoke_hook(VFS_HOOK_CLOSE, "");
    return 0;
}

int fs_vfs_fd_addref(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].in_use) return -1;
    /* BUG-0098 FIX (A2-5): see fs_vfs_close. */
    if (g_fds[fd].refcnt < 1) return -1;
    g_fds[fd].refcnt++;
    return 0;
}

int fs_vfs_stat(const char *path, fs_vfs_stat_t *st) {
    if (!path || !st) return -1;
    fs_vfs_node_t *n = fs_vfs_resolve(path);
    if (!n) return -2;
    memset(st, 0, sizeof(*st));
    st->type = n->type;
    st->size = n->size;
    /* WP-10-wp08fix1: report ownership/permissions/link count from the
     * VFS node (fs-specific stat callbacks may refine them below). */
    st->mode  = n->mode;
    st->uid   = n->uid;
    st->gid   = n->gid;
    st->nlink = n->nlink;
    strncpy(st->name, n->name, VFS_NAME_LEN - 1);
    st->name[VFS_NAME_LEN - 1] = 0;
    if (n->fs_type && n->fs_type->file_ops && n->fs_type->file_ops->stat) {
        return n->fs_type->file_ops->stat(n, st);
    }
    return 0;
}

/* ---- WP-10-wp08fix1: links + permissions ---- */

int fs_vfs_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    fs_vfs_node_t *old = fs_vfs_resolve(oldpath);
    if (!old) return -3;
    char base[VFS_NAME_LEN];
    if (fs_vfs_basename(newpath, base, sizeof(base)) < 0) return -1;
    fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(newpath);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
        !parent->fs_type->dir_ops->link) {
        return -2;   /* backing fs has no hard-link support */
    }
    /* Refuse cross-fs hard links (a real hard link must share the inode).
     * WP-10-wp08fix1 FIX: compare the fs NAME, not the fs_type struct
     * pointer - fs_vfs_mount() sets the mount root's fs_type to the
     * REGISTRY entry while child nodes carry the fs's own struct, so the
     * two pointers differ for the SAME filesystem. */
    if (!old->fs_type || !parent->fs_type ||
        strcmp(old->fs_type->name, parent->fs_type->name) != 0) {
        return -2;
    }
    return parent->fs_type->dir_ops->link(parent, base, old);
}

int fs_vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) return -1;
    char base[VFS_NAME_LEN];
    if (fs_vfs_basename(linkpath, base, sizeof(base)) < 0) return -1;
    fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(linkpath);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
        !parent->fs_type->dir_ops->symlink) {
        return -2;
    }
    return parent->fs_type->dir_ops->symlink(parent, base, target);
}

int fs_vfs_readlink(const char *path, char *buf, int cap) {
    if (!path || !buf || cap <= 0) return -1;
    fs_vfs_node_t *n = fs_vfs_resolve(path);
    if (!n) return -3;
    if (n->type != VFS_TYPE_SYMLINK) return -4;
    if (!n->fs_type || !n->fs_type->dir_ops || !n->fs_type->dir_ops->readlink) {
        return -2;
    }
    return n->fs_type->dir_ops->readlink(n, buf, cap);
}

int fs_vfs_chmod(const char *path, u32 mode) {
    if (!path) return -1;
    fs_vfs_node_t *n = fs_vfs_resolve(path);
    if (!n) return -3;
    n->mode = mode & 07777;   /* only the permission bits are stored */
    return 0;
}

int fs_vfs_chown(const char *path, u32 uid, u32 gid) {
    if (!path) return -1;
    fs_vfs_node_t *n = fs_vfs_resolve(path);
    if (!n) return -3;
    n->uid = uid;
    n->gid = gid;
    return 0;
}

/* ---- Directory operations ---- */

int fs_vfs_mkdir(const char *path) {
    if (!path) return -1;
    char base[VFS_NAME_LEN];
    if (fs_vfs_basename(path, base, sizeof(base)) < 0) return -2;
    fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(path);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
        !parent->fs_type->dir_ops->mkdir) {
        return -3;
    }
    return parent->fs_type->dir_ops->mkdir(parent, base);
}

int fs_vfs_rmdir(const char *path) {
    if (!path) return -1;
    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(path, norm, sizeof(norm)) < 0) return -2;
    /* P0fix1 BUG-0002 (A12-002): refuse to remove a mount point.
     * Removing the cached root node of a mounted filesystem detaches it
     * from the tree while the mount table entry stays in use -> the
     * mount "disappears" (KNOWN_ISSUES BUG-019) and later resolution
     * through the mount table hits freed memory (UAF). */
    if (fs_vfs_find_mount(norm)) return -5;   /* is a mount point */
    char base[VFS_NAME_LEN];
    if (fs_vfs_basename(path, base, sizeof(base)) < 0) return -2;
    fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(path);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops ||
        !parent->fs_type->dir_ops->rmdir) {
        return -3;
    }
    int rc = parent->fs_type->dir_ops->rmdir(parent, base);
    if (rc == 0) {
        /* WP-09-FIX BUG-009: drop the cached node for the removed entry
         * so later lookups cannot hit the stale cached node. */
        fs_vfs_node_t *dead = NULL;
        for (fs_vfs_node_t *c = parent->first_child; c; c = c->next_sibling) {
            if (fs_vfs_cache_name_match(parent, c->name, base)) { dead = c; break; }
        }
        if (dead) fs_vfs_detach_child(dead);
    }
    return rc;
}

int fs_vfs_readdir(const char *path, int index, fs_vfs_dirent_t *entry) {
    if (!path || !entry) return -1;
    fs_vfs_node_t *dir = fs_vfs_resolve(path);
    if (!dir || dir->type != VFS_TYPE_DIR) return -2;
    if (!dir->fs_type || !dir->fs_type->dir_ops ||
        !dir->fs_type->dir_ops->readdir) {
        return -3;
    }
    return dir->fs_type->dir_ops->readdir(dir, index, entry);
}

int fs_vfs_unlink(const char *path) {
    if (!path) return -1;
    char norm[VFS_PATH_LEN];
    if (fs_vfs_normalize(path, norm, sizeof(norm)) < 0) return -2;
    /* P0fix1 BUG-0002 (A12-002): refuse to unlink a mount point
     * (same mount-table lifetime hazard as in fs_vfs_rmdir). */
    if (fs_vfs_find_mount(norm)) return -5;   /* is a mount point */
    char base[VFS_NAME_LEN];
    if (fs_vfs_basename(path, base, sizeof(base)) < 0) return -2;
    fs_vfs_node_t *parent = fs_vfs_resolve_parent_of(path);
    if (!parent || !parent->fs_type || !parent->fs_type->dir_ops) return -3;
    /* If unlink is not implemented, fall back to rmdir (ramfs treats rmdir
     * on a file as an error, so this won't delete files - caller should
     * print "not supported"). */
    if (parent->fs_type->dir_ops->unlink) {
        int rc = parent->fs_type->dir_ops->unlink(parent, base);
        if (rc == 0) {
            /* WP-09-FIX BUG-009: drop the cached node for the deleted
             * entry. Previously `cat` after `rm` on FAT32 still returned
             * the old bytes read from the freed cluster chain via the
             * stale cached node (while `ls` showed the file gone). */
            fs_vfs_node_t *dead = NULL;
            for (fs_vfs_node_t *c = parent->first_child; c; c = c->next_sibling) {
                if (fs_vfs_cache_name_match(parent, c->name, base)) { dead = c; break; }
            }
            if (dead) fs_vfs_detach_child(dead);
        }
        return rc;
    }
    return -4;
}

int fs_vfs_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    /* Resolve old parent + name. */
    char oldbase[VFS_NAME_LEN];
    if (fs_vfs_basename(oldpath, oldbase, sizeof(oldbase)) < 0) return -2;
    fs_vfs_node_t *oldparent = fs_vfs_resolve_parent_of(oldpath);
    if (!oldparent) return -3;
    /* Resolve new parent + name. */
    char newbase[VFS_NAME_LEN];
    if (fs_vfs_basename(newpath, newbase, sizeof(newbase)) < 0) return -4;
    fs_vfs_node_t *newparent = fs_vfs_resolve_parent_of(newpath);
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

int fs_vfs_register_hook(int (*hook)(int op, const char *path)) {
    if (g_hook) return -1;
    g_hook = hook;
    return 0;
}

/* ---- Diagnostics ---- */

void fs_vfs_list_mounts(void) {
    screen_console_puts("VFS mount table:\n");
    int any = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].in_use) continue;
        any = 1;
        /* BUG-0181 FIX (A12-016): the old code strcpy()'d mount_point,
         * fs_type and device into `line[VFS_PATH_LEN + 64]` (320 B).
         * Worst case is 2 + 255 (mount_point) + 2 + 15 (fs_type) +
         * 7 + 63 (device) + 1 + 1 + 1 = 347 B -> up to ~27 bytes of
         * stack overrun with long field values. The buffer is now sized
         * for the true worst case AND every append is bounds-clamped
         * (snprintf-style), so no field combination can write past it. */
        char line[VFS_PATH_LEN + VFS_NAME_LEN + VFS_NAME_LEN + 32];
        int pos = 0;
        {
            /* Bounded append helper: copies src into line+pos without
             * ever exceeding the buffer, always NUL-terminates. */
            #define VFS_LM_APPEND(s) do { \
                const char *src_ = (s); \
                while (*src_ && pos < (int)sizeof(line) - 1) { \
                    line[pos++] = *src_++; \
                } \
                line[pos] = 0; \
            } while (0)
            VFS_LM_APPEND("  ");
            VFS_LM_APPEND(g_mounts[i].mount_point);
            VFS_LM_APPEND("  ");
            VFS_LM_APPEND(g_mounts[i].fs_type ? g_mounts[i].fs_type->name : "?");
            if (g_mounts[i].device[0]) {
                VFS_LM_APPEND("  (dev=");
                VFS_LM_APPEND(g_mounts[i].device);
                VFS_LM_APPEND(")");
            }
            VFS_LM_APPEND("\n");
            #undef VFS_LM_APPEND
        }
        screen_console_puts(line);
    }
    if (!any) screen_console_puts("  (no mounts)\n");
}

/* P2-06 FIX: read-only access to the mount table for commands like df. */
int fs_vfs_get_mounts(fs_vfs_mount_info_t *out, int max) {
    if (!out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS && count < max; i++) {
        if (!g_mounts[i].in_use) continue;
        fs_vfs_mount_info_t *e = &out[count];
        memset(e, 0, sizeof(*e));
        strncpy(e->mount_point, g_mounts[i].mount_point,
                   sizeof(e->mount_point) - 1);
        if (g_mounts[i].fs_type) {
            strncpy(e->fs_type, g_mounts[i].fs_type->name,
                       sizeof(e->fs_type) - 1);
        }
        strncpy(e->device, g_mounts[i].device, sizeof(e->device) - 1);
        e->in_use = 1;
        count++;
    }
    return count;
}
