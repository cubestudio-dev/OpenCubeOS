/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - core/lib path resolution (see lib_fspath.h
 * for provenance; bodies are the verbatim WP-05 rescue with only the
 * shell_ prefixes renamed to fspath_). */
#include "lib_fspath.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "fs_vfs.h"

static char g_kernel_cwd[FSPATH_CWD_LEN] = "/";

const char *fspath_kernel_cwd(void) {
    return g_kernel_cwd;
}

/* Normalize an absolute path in-place. Handles "." and ".." and "//".
 * Returns 0 on success, -1 on overflow.  (BUG-0236: the component
 * table is heap-allocated - the old 8 KiB stack array left under
 * 2 KiB of headroom on the 16 KiB kernel stack.) */
int fspath_normalize(char *path, int maxlen) {
    if (!path || maxlen < 2) return -1;
    if (path[0] != '/') return -1;

    char (*comps)[VFS_NAME_LEN] = (char (*)[VFS_NAME_LEN])kmalloc(
        (u64)sizeof(char[VFS_NAME_LEN]) * (u64)(FSPATH_CWD_LEN / 2));
    if (!comps) return -1;
    int ncomp = 0;
    int i = 0;
    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;
        char comp[VFS_NAME_LEN];
        int ci = 0;
        while (path[i] && path[i] != '/' && ci < VFS_NAME_LEN - 1) {
            comp[ci++] = path[i++];
        }
        comp[ci] = 0;
        if (strcmp(comp, ".") == 0) {
            /* skip */
        } else if (strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if (ncomp >= FSPATH_CWD_LEN / 2) { kfree(comps); return -1; }
            strncpy(comps[ncomp], comp, VFS_NAME_LEN - 1);
            comps[ncomp][VFS_NAME_LEN - 1] = 0;
            ncomp++;
        }
    }
    /* Rebuild. */
    int p = 0;
    path[p++] = '/';
    for (int c = 0; c < ncomp; c++) {
        int len = (int)strlen(comps[c]);
        if (p + len + 1 >= maxlen) { kfree(comps); return -1; }
        memcpy(path + p, comps[c], (usize)len);
        p += len;
        if (c < ncomp - 1) path[p++] = '/';
    }
    path[p] = 0;
    kfree(comps);
    return 0;
}

int fspath_set_kernel_cwd(const char *path) {
    if (!path || !path[0]) return -1;
    char newcwd[FSPATH_CWD_LEN];
    if (path[0] == '/') {
        /* Absolute. */
        strncpy(newcwd, path, FSPATH_CWD_LEN - 1);
        newcwd[FSPATH_CWD_LEN - 1] = 0;
    } else {
        /* Relative - prepend cwd. */
        int cwdlen = (int)strlen(g_kernel_cwd);
        if (cwdlen + 1 + (int)strlen(path) + 1 > FSPATH_CWD_LEN) return -1;
        strcpy(newcwd, g_kernel_cwd);
        if (g_kernel_cwd[cwdlen - 1] != '/') {
            newcwd[cwdlen++] = '/';
            newcwd[cwdlen] = 0;
        }
        strcpy(newcwd + cwdlen, path);
    }
    if (fspath_normalize(newcwd, FSPATH_CWD_LEN) < 0) return -1;
    /* Verify the path exists in the VFS. */
    fs_vfs_stat_t st;
    if (fs_vfs_stat(newcwd, &st) < 0) return -2;
    if (st.type != VFS_TYPE_DIR) return -3;
    strcpy(g_kernel_cwd, newcwd);
    return 0;
}

/* BUG-0278 rules: resolve against a CALLER-SUPPLIED cwd. */
int fspath_resolve_base(const char *cwd, const char *path, char *out, int out_len) {
    if (!path || !out || out_len < 2) return -1;
    if (!cwd || !cwd[0]) cwd = "/";
    if (path[0] == '/') {
        strncpy(out, path, out_len - 1);
        out[out_len - 1] = 0;
    } else {
        int cwdlen = (int)strlen(cwd);
        if (cwdlen + 1 + (int)strlen(path) + 1 > out_len) return -1;
        strcpy(out, cwd);
        if (cwd[cwdlen - 1] != '/') {
            out[cwdlen++] = '/';
            out[cwdlen] = 0;
        }
        strcpy(out + cwdlen, path);
    }
    return fspath_normalize(out, out_len);
}

int fspath_resolve(const char *path, char *out, int out_len) {
    return fspath_resolve_base(g_kernel_cwd, path, out, out_len);
}

/* P0-8: two alternating static buffers so two resolutions in one
 * expression don't clobber each other. */
static char g_resolve_buf[2][FSPATH_CWD_LEN];
static int g_resolve_slot = 0;

const char *fspath_resolve_static(const char *path) {
    int slot = g_resolve_slot;
    g_resolve_slot = (g_resolve_slot + 1) % 2;
    if (fspath_resolve(path, g_resolve_buf[slot], FSPATH_CWD_LEN) < 0) {
        return path;  /* fall back to the raw input */
    }
    return g_resolve_buf[slot];
}
