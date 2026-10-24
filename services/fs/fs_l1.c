/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/fs L1 surface.
 *
 * Ring-3 dispatch for the filesystem service (SYS_L1SVC cat "fs").
 * Six operations carry the exact command bodies of the v1 oc>
 * commands mount / mounts / umount / fatmount / fatstat / tree; the
 * output sink is the caller's text buffer, so ush owns presentation.
 * The rest of the v1 fs command family (cat ls cp mv rm mkdir rmdir
 * touch grep df du stat ln chmod chown vi nano head tail sort uniq
 * sed awk wc) already runs in ring 3 through the classic file
 * syscalls - those keep their ush implementations and are NOT
 * duplicated here.
 *
 * Relative paths resolve against the CALLING PROCESS's cwd, exactly
 * like the classic file syscalls (resolve_user_path semantics).
 */
#include "fs_vfs.h"
#include "fs_fat32.h"
#include "core_registry.h"
#include "l1svc_defs.h"
#include "core_usermode.h"
#include "shell.h"
#include "lib_string.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* output-buffer helpers                                              */
/* ------------------------------------------------------------------ */
static char *s_out;
static int   s_left;

static void put(const char *s) {
    while (*s && s_left > 1) { *s_out++ = *s++; s_left--; }
}
static void put_u64(u64 v) {
    char b[24];
    u64_to_str(v, b);
    put(b);
}

/* Resolve `path` against the calling process's cwd (ring-3 caller)
 * or the kernel shell cwd (oc> fallback).  out is VFS_PATH_LEN. */
static int l1_resolve(const char *path, char *out, int out_len) {
    if (!path || !path[0]) {
        if (out_len > 0) out[0] = '/';
        if (out_len > 1) out[1] = 0;
        return 0;
    }
    if (path[0] == '/') {
        int i = 0;
        while (path[i] && i < out_len - 1) { out[i] = path[i]; i++; }
        out[i] = 0;
        return 0;
    }
    user_proc_t *proc = user_process_current();
    if (proc) {
        return shell_resolve_path_base(proc->cwd, path, out, out_len);
    }
    return shell_resolve_path(path, out, out_len);
}

/* ------------------------------------------------------------------ */
/* tree (L1SVC_FS_TREE) - v1 body from recovery/shell_cmds_file.c     */
/* ------------------------------------------------------------------ */
static void tree_walk(const char *path, int depth, int *files, int *dirs) {
    /* P2-25 FIX carried over: limit recursion depth. */
    if (depth > 16) return;
    for (int i = 0; ; i++) {
        fs_vfs_dirent_t e;
        if (fs_vfs_readdir(path, i, &e) < 0) break;
        char line[VFS_PATH_LEN + 64];
        int p = 0;
        for (int d = 0; d < depth && p < (int)sizeof(line) - 4; d++) {
            line[p++] = ' ';
            line[p++] = ' ';
        }
        if (e.type == VFS_TYPE_DIR) {
            line[p++] = '+';
            line[p++] = ' ';
            {
                int el = (int)strlen(e.name);
                int k = 0;
                while (e.name[k] && p < (int)sizeof(line) - 4) line[p++] = e.name[k++];
                (void)el;
            }
            line[p++] = '/';
            line[p++] = '\n';
            line[p] = 0;
            put(line);
            (*dirs)++;
            /* BUG-0087 FIX (A15-8) carried over: depth cap + bounded
             * child join (deep trees truncated, never overflowed). */
            if (depth >= 12) {
                put("...\n");
                return;
            }
            char child[VFS_PATH_LEN];
            int pl = (int)strlen(path);
            int need_slash = (pl > 0 && path[pl - 1] != '/');
            int el = (int)strlen(e.name);
            if (pl + need_slash + el + 1 > (int)sizeof(child)) continue;
            strcpy(child, path);
            if (need_slash) {
                child[pl++] = '/';
                child[pl] = 0;
            }
            strcpy(child + pl, e.name);
            tree_walk(child, depth + 1, files, dirs);
        } else {
            line[p++] = ' ';
            line[p++] = ' ';
            {
                int k = 0;
                while (e.name[k] && p < (int)sizeof(line) - 2) line[p++] = e.name[k++];
            }
            line[p++] = '\n';
            line[p] = 0;
            put(line);
            (*files)++;
        }
    }
}

static int l1_fs_tree(const char *args) {
    char path[VFS_PATH_LEN];
    if (l1_resolve(args, path, (int)sizeof(path)) != 0) {
        put("tree: bad path\n");
        return 1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) < 0) {
        put("tree: no such path\n");
        return 1;
    }
    put(path);
    put("\n");
    int files = 0, dirs = 0;
    tree_walk(path, 0, &files, &dirs);
    put("\n");
    put_u64((u64)dirs);
    put(" directories, ");
    put_u64((u64)files);
    put(" files\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* mount / mounts (L1SVC_FS_MOUNT / L1SVC_FS_MOUNTS)                   */
/* ------------------------------------------------------------------ */
static int l1_fs_mount(const char *args) {
    /* P2 fix carried over: mount <type> <device> <path> or list. */
    if (!args || !args[0]) {
        /* Shared renderer (fs_vfs_list_mounts_str). */
        char chunk[1024];
        fs_vfs_list_mounts_str(chunk, (int)sizeof(chunk));
        put(chunk);
        return 0;
    }
    char type[32], dev[32], path[64];
    int i = 0, j = 0;
    while (args[i] && args[i] != ' ' && j < 31) { type[j++] = args[i++]; }
    type[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j < 31) { dev[j++] = args[i++]; }
    dev[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && j < 63) { path[j++] = args[i++]; }
    path[j] = 0;
    if (!type[0] || !dev[0] || !path[0]) {
        put("usage: mount <type> <device> <path>\n");
        return 1;
    }
    /* Ensure mount point exists (same as v1). */
    fs_vfs_mkdir(path);
    if (fs_vfs_mount(type, path, dev) != 0) {
        put("mount: failed\n");
        return 1;
    }
    put("mounted ");
    put(type);
    put(" on ");
    put(path);
    put("\n");
    return 0;
}

static int l1_fs_mounts(const char *args) {
    (void)args;
    char chunk[1024];
    fs_vfs_list_mounts_str(chunk, (int)sizeof(chunk));
    put(chunk);
    return 0;
}

/* ------------------------------------------------------------------ */
/* umount (L1SVC_FS_UMOUNT)                                            */
/* ------------------------------------------------------------------ */
static int l1_fs_umount(const char *args) {
    if (!args || !args[0]) { put("usage: umount <path>\n"); return 1; }
    char resolved[VFS_PATH_LEN];
    if (l1_resolve(args, resolved, (int)sizeof(resolved)) != 0) {
        put("umount: bad path\n");
        return 1;
    }
    /* BUG-0133 FIX carried over: report the refusal reason. */
    int rc = fs_vfs_umount(resolved);
    if (rc == -4) { put("umount: cannot unmount the root filesystem\n"); return 1; }
    if (rc == -5) { put("umount: target is busy - close open files first\n"); return 1; }
    if (rc == -3) { put("umount: unmount nested mounts first\n"); return 1; }
    if (rc < 0)   { put("umount: failed (not mounted?)\n"); return 1; }
    put("umounted ");
    put(resolved);
    put("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* fatmount / fatstat (L1SVC_FS_FATMOUNT / L1SVC_FS_FATSTAT)           */
/* ------------------------------------------------------------------ */
static int l1_fs_fatmount(const char *args) {
    /* usage carried over: fatmount <device> <mount_point> (ata0 /mnt) */
    char dev[16] = "ata0";
    char mnt[VFS_PATH_LEN] = "/mnt";
    if (args && args[0]) {
        int i = 0, j = 0;
        while (args[i] && args[i] != ' ' && i < 15) { dev[j++] = args[i++]; }
        dev[j] = 0;
        while (args[i] == ' ') i++;
        j = 0;
        while (args[i] && j < VFS_PATH_LEN - 1) { mnt[j++] = args[i++]; }
        mnt[j] = 0;
    }
    /* P2-18 FIX carried over: do NOT pre-create the mount point. */
    int rc = fs_fat32_mount(dev, mnt);
    if (rc < 0) {
        put("fatmount: ");
        put(dev);
        put(" -> ");
        put(mnt);
        put(" FAILED - no FAT32 partition on device or drive not present\n");
        put("(the mount point was NOT created - `ls ");
        put(mnt);
        put("` will report 'no such path' until a real mount succeeds)\n");
        return 1;
    }
    put("fat32 mounted. Try: ls ");
    put(mnt);
    put("\n");
    return 0;
}

static int l1_fs_fatstat(const char *args) {
    (void)args;
    u64 ts, fc; u32 cs;
    fs_fat32_get_stats(&ts, &fc, &cs);
    put("fat32: sectors=");
    put_u64(ts);
    put(" free_clusters=");
    put_u64(fc);
    put(" cluster_size=");
    put_u64((u64)cs);
    put("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* SYS_L1SVC entry point                                              */
/* ------------------------------------------------------------------ */
int fs_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
        case L1SVC_FS_MOUNT:    return l1_fs_mount(a);
        case L1SVC_FS_MOUNTS:   return l1_fs_mounts(a);
        case L1SVC_FS_UMOUNT:   return l1_fs_umount(a);
        case L1SVC_FS_FATMOUNT: return l1_fs_fatmount(a);
        case L1SVC_FS_FATSTAT:  return l1_fs_fatstat(a);
        case L1SVC_FS_TREE:     return l1_fs_tree(a);
        default: return -1;
    }
}

/* v2.0.0 file-local service identity: category == services/fs folder;
 * ring-3 surface is fs_l1_dispatch (the VFS bring-up itself stays in
 * kmain - ramfs must exist before anything can mount). */
OC_SERVICE_DECL_L1("fs", "filesystem service (VFS + ramfs/FAT32/ext4/exFAT + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   fs_l1_dispatch);
