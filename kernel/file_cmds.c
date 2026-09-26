/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/file_cmds.c
 * Purpose: Shell commands for file operations on the VFS.
 *
 * Implements the 16 WP-05/WP-07 shell file commands:
 *   ls, cd, pwd, cat, mkdir, rmdir, touch, rm, mv, cp, echo, tree, df, du,
 *   mount, umount
 *
 * All commands resolve relative paths against the shell's cwd (via
 * shell_resolve_path_static). Output redirection (>, >>) and pipes (|) are
 * handled by the shell parser in shell.c; the commands here just write to
 * oc_console_* and the shell captures as needed.
 */
#include "file_cmds.h"
#include "shell.h"
#include "vfs.h"
#include "ramfs.h"
#include "fat32.h"
#include "console.h"
#include "string.h"
#include "heap.h"

/* ---- Helpers ---- */

/* Split args on the first whitespace. Returns the length of the first token
 * (writes it into first, max first_len). Sets *rest to point past the
 * whitespace (to the second token, or to the null terminator if none). */
static int split_first(const char *args, char *first, int first_len,
                       const char **rest) {
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && i < first_len - 1) {
        first[i] = args[i];
        i++;
    }
    first[i] = 0;
    while (args[i] == ' ' || args[i] == '\t') i++;
    if (rest) *rest = args + i;
    return i;
}

/* Format a u64 into the buffer (decimal). */
static void fmt_u64(u64 v, char *out) {
    oc_u64_to_str(v, out);
}

/* Print "size name" line for a directory entry. */
static void print_dirent(const char *name, int type, u64 size) {
    char line[VFS_NAME_LEN + 40];
    oc_strcpy(line, "  ");
    if (type == VFS_TYPE_DIR) {
        oc_strcpy(line + oc_strlen(line), "[D] ");
    } else if (type == VFS_TYPE_DEVICE) {
        oc_strcpy(line + oc_strlen(line), "[C] ");
    } else {
        /* File: pad size right-aligned in 10 chars. */
        char num[24];
        fmt_u64(size, num);
        int nl = (int)oc_strlen(num);
        /* BUG-005 FIX: The old padding loop called oc_strlen(line) twice
         * per iteration. After the first call wrote a space at the null
         * terminator position, the second oc_strlen(line) scanned into
         * uninitialized stack memory (the null was overwritten). This
         * was a Heisenbug: -O0 happened to have zeros on the stack so
         * strlen returned the right value, but -O2's different stack
         * layout caused garbage reads, producing corrupted output.
         * Fix: compute the position once, write space + null together. */
        for (int i = nl; i < 10; i++) {
            int pos = (int)oc_strlen(line);
            line[pos] = ' ';
            line[pos + 1] = '\0';
        }
        oc_strcpy(line + oc_strlen(line), num);
        oc_strcpy(line + oc_strlen(line), " ");
    }
    oc_strcpy(line + oc_strlen(line), name);
    oc_strcpy(line + oc_strlen(line), "\n");
    oc_console_puts(line);
}

/* ---- ls ---- */
static int cmd_ls(const char *args) {
    const char *path = (args && args[0]) ?
        shell_resolve_path_static(args) : shell_get_cwd();
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) {
        oc_console_puts("ls: no such path: ");
        oc_console_puts(path);
        oc_console_putc('\n');
        return 1;
    }
    if (st.type != VFS_TYPE_DIR) {
        /* It's a file - print its name and size. */
        print_dirent(st.name, st.type, st.size);
        return 0;
    }
    for (int i = 0; ; i++) {
        vfs_dirent_t e;
        if (vfs_readdir(path, i, &e) < 0) break;
        /* Stat each entry to get size for files. */
        vfs_stat_t es;
        u64 sz = 0;
        /* Build child path. */
        char child[VFS_PATH_LEN];
        int pl = (int)oc_strlen(path);
        oc_strcpy(child, path);
        if (pl > 0 && child[pl - 1] != '/') {
            child[pl++] = '/';
            child[pl] = 0;
        }
        oc_strcpy(child + pl, e.name);
        if (vfs_stat(child, &es) == 0) sz = es.size;
        print_dirent(e.name, e.type, sz);
    }
    return 0;
}

/* ---- cd ---- */
static int cmd_cd(const char *args) {
    if (!args || !args[0]) {
        /* cd with no args - go to /. */
        shell_set_cwd("/");
        return 0;
    }
    if (shell_set_cwd(args) < 0) {
        oc_console_puts("cd: no such directory: ");
        oc_console_puts(args);
        oc_console_putc('\n');
        return 1;
    }
    return 0;
}

/* ---- pwd ---- */
static int cmd_pwd(const char *args) {
    (void)args;
    oc_console_puts(shell_get_cwd());
    oc_console_putc('\n');
    return 0;
}

/* ---- cat ---- */
/* Handles: cat <file> [<file>...] - cat each file in order.
 *          cat (no args, stdin available) - read from stdin (pipe). */
static int cmd_cat(const char *args) {
    if (!args || !args[0]) {
        /* No file args - try stdin (pipe). */
        if (shell_has_stdin()) {
            char buf[512];
            int n;
            while ((n = shell_read_stdin(buf, sizeof(buf))) > 0) {
                for (int i = 0; i < n; i++) oc_console_putc(buf[i]);
            }
            return 0;
        }
        oc_console_puts("usage: cat <file>\n");
        return 1;
    }
    /* Iterate over space-separated file args (so wildcards work). */
    const char *p = args;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char path[VFS_PATH_LEN];
        const char *next;
        split_first(p, path, sizeof(path), &next);
        if (path[0]) {
            const char *resolved = shell_resolve_path_static(path);
            int fd = vfs_open(resolved, VFS_O_RDONLY);
            if (fd < 0) {
                oc_console_puts("cat: cannot open ");
                oc_console_puts(resolved);
                oc_console_putc('\n');
            } else {
                char buf[512];
                int n;
                while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
                    for (int i = 0; i < n; i++) oc_console_putc(buf[i]);
                }
                vfs_close(fd);
            }
        }
        p = next;
    }
    return 0;
}

/* ---- mkdir ---- */
static int cmd_mkdir(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: mkdir <path>\n"); return 1; }
    /* Handle multiple space-separated paths. */
    const char *p = args;
    int err = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char path[VFS_PATH_LEN];
        const char *next;
        split_first(p, path, sizeof(path), &next);
        if (path[0]) {
            const char *resolved = shell_resolve_path_static(path);
            if (vfs_mkdir(resolved) < 0) {
                oc_console_puts("mkdir: failed: ");
                oc_console_puts(resolved);
                oc_console_putc('\n');
                err = 1;
            }
        }
        p = next;
    }
    return err;
}

/* ---- rmdir ---- */
static int cmd_rmdir(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: rmdir <path>\n"); return 1; }
    const char *resolved = shell_resolve_path_static(args);
    if (vfs_rmdir(resolved) < 0) {
        oc_console_puts("rmdir: failed (not empty or not a directory?)\n");
        return 1;
    }
    return 0;
}

/* ---- touch ---- */
static int cmd_touch(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: touch <path>\n"); return 1; }
    const char *resolved = shell_resolve_path_static(args);
    /* If it exists, do nothing (real touch would update mtime). */
    vfs_stat_t st;
    if (vfs_stat(resolved, &st) == 0) return 0;
    int fd = vfs_open(resolved, VFS_O_RDWR | VFS_O_CREAT);
    if (fd < 0) {
        oc_console_puts("touch: create failed\n");
        return 1;
    }
    vfs_close(fd);
    return 0;
}

/* ---- rm ---- */
static int cmd_rm(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: rm <file>\n"); return 1; }
    /* Iterate over space-separated paths. */
    const char *p = args;
    int err = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char path[VFS_PATH_LEN];
        const char *next;
        split_first(p, path, sizeof(path), &next);
        if (path[0]) {
            const char *resolved = shell_resolve_path_static(path);
            if (vfs_unlink(resolved) < 0) {
                oc_console_puts("rm: cannot remove ");
                oc_console_puts(resolved);
                oc_console_puts(" (not a file or unsupported fs)\n");
                err = 1;
            }
        }
        p = next;
    }
    return err;
}

/* ---- mv ---- */
/* Tries vfs_rename first. If that fails (cross-fs or no rename op), falls
 * back to copy+unlink. */
static int cmd_mv(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: mv <src> <dst>\n"); return 1; }
    char src[VFS_PATH_LEN];
    const char *rest;
    split_first(args, src, sizeof(src), &rest);
    if (!src[0] || !rest[0]) { oc_console_puts("usage: mv <src> <dst>\n"); return 1; }
    char dst[VFS_PATH_LEN];
    split_first(rest, dst, sizeof(dst), NULL);
    if (!dst[0]) { oc_console_puts("usage: mv <src> <dst>\n"); return 1; }

    const char *rsrc = shell_resolve_path_static(src);
    const char *rdst = shell_resolve_path_static(dst);

    /* Try rename first. */
    if (vfs_rename(rsrc, rdst) == 0) return 0;

    /* Fallback: copy + unlink. */
    /* If dst is a directory, append src basename. */
    vfs_stat_t dst_st;
    if (vfs_stat(rdst, &dst_st) == 0 && dst_st.type == VFS_TYPE_DIR) {
        /* Append basename of src to dst. */
        const char *bn = rsrc + oc_strlen(rsrc);
        while (bn > rsrc && *(bn - 1) != '/') bn--;
        char newdst[VFS_PATH_LEN];
        int dl = (int)oc_strlen(rdst);
        oc_strcpy(newdst, rdst);
        if (dl > 0 && newdst[dl - 1] != '/') {
            newdst[dl++] = '/';
            newdst[dl] = 0;
        }
        oc_strcpy(newdst + dl, bn);
        rdst = newdst;
    }

    /* Read source. */
    int fd = vfs_open(rsrc, VFS_O_RDONLY);
    if (fd < 0) {
        oc_console_puts("mv: cannot open source\n");
        return 1;
    }
    /* Get source size. */
    vfs_stat_t ss;
    if (vfs_stat(rsrc, &ss) < 0) ss.size = 65536;
    u64 total = ss.size;
    if (total > 1024 * 1024) total = 1024 * 1024;  /* 1 MiB cap */

    char *buf = (char *)kmalloc(total > 0 ? total : 1);
    if (!buf) {
        oc_console_puts("mv: out of memory\n");
        vfs_close(fd);
        return 1;
    }
    int total_read = 0;
    int n;
    while (total_read < (int)total &&
           (n = vfs_read(fd, buf + total_read, (int)total - total_read)) > 0) {
        total_read += n;
    }
    vfs_close(fd);

    /* Write to dst. */
    int wfd = vfs_open(rdst, VFS_O_WRONLY | VFS_O_CREAT);
    if (wfd < 0) {
        oc_console_puts("mv: cannot create destination\n");
        kfree(buf);
        return 1;
    }
    int written = 0;
    while (written < total_read) {
        int w = vfs_write(wfd, buf + written, total_read - written);
        if (w <= 0) break;
        written += w;
    }
    vfs_close(wfd);
    kfree(buf);

    /* Unlink source. */
    if (vfs_unlink(rsrc) < 0) {
        oc_console_puts("mv: copied but could not remove source\n");
        return 1;
    }
    return 0;
}

/* ---- cp ---- */
static int cmd_cp(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: cp <src> <dst>\n"); return 1; }
    char src[VFS_PATH_LEN];
    const char *rest;
    split_first(args, src, sizeof(src), &rest);
    if (!src[0] || !rest[0]) { oc_console_puts("usage: cp <src> <dst>\n"); return 1; }
    char dst[VFS_PATH_LEN];
    split_first(rest, dst, sizeof(dst), NULL);
    if (!dst[0]) { oc_console_puts("usage: cp <src> <dst>\n"); return 1; }

    const char *rsrc = shell_resolve_path_static(src);
    const char *rdst = shell_resolve_path_static(dst);

    /* If dst is a directory, append src basename. */
    vfs_stat_t dst_st;
    if (vfs_stat(rdst, &dst_st) == 0 && dst_st.type == VFS_TYPE_DIR) {
        const char *bn = rsrc + oc_strlen(rsrc);
        while (bn > rsrc && *(bn - 1) != '/') bn--;
        char newdst[VFS_PATH_LEN];
        int dl = (int)oc_strlen(rdst);
        oc_strcpy(newdst, rdst);
        if (dl > 0 && newdst[dl - 1] != '/') {
            newdst[dl++] = '/';
            newdst[dl] = 0;
        }
        oc_strcpy(newdst + dl, bn);
        /* Copy into a fresh static buffer for the duration of this call. */
        static char cp_dst[VFS_PATH_LEN];
        oc_strcpy(cp_dst, newdst);
        rdst = cp_dst;
    }

    int fd = vfs_open(rsrc, VFS_O_RDONLY);
    if (fd < 0) {
        oc_console_puts("cp: cannot open source\n");
        return 1;
    }
    vfs_stat_t ss;
    if (vfs_stat(rsrc, &ss) < 0) ss.size = 65536;
    u64 total = ss.size;
    if (total > 1024 * 1024) total = 1024 * 1024;
    char *buf = (char *)kmalloc(total > 0 ? total : 1);
    if (!buf) {
        oc_console_puts("cp: out of memory\n");
        vfs_close(fd);
        return 1;
    }
    int total_read = 0;
    int n;
    while (total_read < (int)total &&
           (n = vfs_read(fd, buf + total_read, (int)total - total_read)) > 0) {
        total_read += n;
    }
    vfs_close(fd);

    int wfd = vfs_open(rdst, VFS_O_WRONLY | VFS_O_CREAT);
    if (wfd < 0) {
        oc_console_puts("cp: cannot create destination\n");
        kfree(buf);
        return 1;
    }
    int written = 0;
    while (written < total_read) {
        int w = vfs_write(wfd, buf + written, total_read - written);
        if (w <= 0) break;
        written += w;
    }
    vfs_close(wfd);
    kfree(buf);
    return 0;
}

/* ---- tree ---- */
/* Recursive tree printer. depth is the current indentation level. */
static void tree_walk(const char *path, int depth, int *files, int *dirs) {
    /* P2-25 FIX: limit recursion depth to prevent stack overflow. */
    if (depth > 16) return;
    for (int i = 0; ; i++) {
        vfs_dirent_t e;
        if (vfs_readdir(path, i, &e) < 0) break;
        /* Indent. */
        char line[VFS_PATH_LEN + 64];
        int p = 0;
        for (int d = 0; d < depth; d++) {
            line[p++] = ' ';
            line[p++] = ' ';
        }
        if (e.type == VFS_TYPE_DIR) {
            line[p++] = '+';
            line[p++] = ' ';
            oc_strcpy(line + p, e.name);
            oc_strcpy(line + oc_strlen(line), "/\n");
            oc_console_puts(line);
            (*dirs)++;
            /* Recurse. */
            char child[VFS_PATH_LEN];
            int pl = (int)oc_strlen(path);
            oc_strcpy(child, path);
            if (pl > 0 && child[pl - 1] != '/') {
                child[pl++] = '/';
                child[pl] = 0;
            }
            oc_strcpy(child + pl, e.name);
            tree_walk(child, depth + 1, files, dirs);
        } else {
            line[p++] = '-';
            line[p++] = ' ';
            oc_strcpy(line + p, e.name);
            oc_strcpy(line + oc_strlen(line), "\n");
            oc_console_puts(line);
            (*files)++;
        }
    }
}

static int cmd_tree(const char *args) {
    const char *path = (args && args[0]) ?
        shell_resolve_path_static(args) : shell_get_cwd();
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) {
        oc_console_puts("tree: no such path\n");
        return 1;
    }
    oc_console_puts(path);
    oc_console_putc('\n');
    int files = 0, dirs = 0;
    tree_walk(path, 0, &files, &dirs);
    char summary[80]; char num[20];
    oc_strcpy(summary, "\n");
    fmt_u64((u64)dirs, num);  oc_strcpy(summary + oc_strlen(summary), num);
    oc_strcpy(summary + oc_strlen(summary), " directories, ");
    fmt_u64((u64)files, num); oc_strcpy(summary + oc_strlen(summary), num);
    oc_strcpy(summary + oc_strlen(summary), " files\n");
    oc_console_puts(summary);
    return 0;
}

/* ---- df ---- */
static int cmd_df(const char *args) {
    (void)args;
    oc_console_puts("Filesystem     Mount     Device    Use\n");
    /* ramfs stats. */
    int nn, ss;
    ramfs_get_stats(&nn, &ss);
    char line[120]; char num[20];
    oc_strcpy(line, "ramfs          /         -         ");
    fmt_u64((u64)nn, num); oc_strcpy(line + oc_strlen(line), num);
    oc_strcpy(line + oc_strlen(line), " nodes, ");
    fmt_u64((u64)ss, num); oc_strcpy(line + oc_strlen(line), num);
    oc_strcpy(line + oc_strlen(line), " bytes\n");
    oc_console_puts(line);
    /* FAT32 stats if mounted. */
    u64 ts, fc; u32 cs;
    fat32_get_stats(&ts, &fc, &cs);
    if (ts > 0) {
        oc_strcpy(line, "fat32          /mnt      ata0      ");
        fmt_u64(fc, num); oc_strcpy(line + oc_strlen(line), num);
        oc_strcpy(line + oc_strlen(line), " free clusters, ");
        fmt_u64((u64)cs, num); oc_strcpy(line + oc_strlen(line), num);
        oc_strcpy(line + oc_strlen(line), " bytes/cluster\n");
        oc_console_puts(line);
    }
    return 0;
}

/* ---- du ---- */
/* Recursively sum file sizes under path. */
/* BUG-031 FIX: Add depth limit to prevent stack overflow on deep
 * directory trees (same fix as tree_walk's P2-25). */
static u64 du_walk(const char *path, int depth) {
    if (depth > 16) return 0;  /* prevent stack overflow */
    u64 total = 0;
    for (int i = 0; ; i++) {
        vfs_dirent_t e;
        if (vfs_readdir(path, i, &e) < 0) break;
        char child[VFS_PATH_LEN];
        int pl = (int)oc_strlen(path);
        oc_strcpy(child, path);
        if (pl > 0 && child[pl - 1] != '/') {
            child[pl++] = '/';
            child[pl] = 0;
        }
        oc_strcpy(child + pl, e.name);
        vfs_stat_t st;
        if (vfs_stat(child, &st) < 0) continue;
        if (e.type == VFS_TYPE_DIR) {
            total += du_walk(child, depth + 1);
        } else {
            total += st.size;
        }
    }
    return total;
}

static int cmd_du(const char *args) {
    const char *path = (args && args[0]) ?
        shell_resolve_path_static(args) : shell_get_cwd();
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) {
        oc_console_puts("du: no such path\n");
        return 1;
    }
    u64 total = (st.type == VFS_TYPE_DIR) ? du_walk(path, 0) : st.size;
    char line[VFS_PATH_LEN + 40]; char num[20];
    fmt_u64(total, num);
    oc_strcpy(line, num);
    oc_strcpy(line + oc_strlen(line), "\t");
    oc_strcpy(line + oc_strlen(line), path);
    oc_strcpy(line + oc_strlen(line), "\n");
    oc_console_puts(line);
    return 0;
}

/* ---- mount ---- */
static int cmd_mount(const char *args) {
    /* P2 fix: mount <type> <device> <path> or just "mount" to list. */
    if (!args || !args[0]) {
        vfs_list_mounts();
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
        oc_console_puts("usage: mount <type> <device> <path>\n");
        return 1;
    }
    /* Ensure mount point exists. */
    vfs_mkdir(path);
    if (vfs_mount(type, path, dev) != 0) {
        oc_console_puts("mount: failed\n");
        return 1;
    }
    oc_console_puts("mounted ");
    oc_console_puts(type);
    oc_console_puts(" on ");
    oc_console_puts(path);
    oc_console_putc('\n');
    return 0;
}

/* ---- umount ---- */
static int cmd_umount(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: umount <path>\n"); return 1; }
    const char *resolved = shell_resolve_path_static(args);
    if (vfs_umount(resolved) < 0) {
        oc_console_puts("umount: failed (not mounted?)\n");
        return 1;
    }
    oc_console_puts("umounted ");
    oc_console_puts(resolved);
    oc_console_putc('\n');
    return 0;
}

/* ---- write ---- */
/* usage: write <path> <text...> */
static int cmd_write(const char *args) {
    if (!args || !args[0]) { oc_console_puts("usage: write <path> <text>\n"); return 1; }
    char path[VFS_PATH_LEN];
    const char *rest;
    split_first(args, path, sizeof(path), &rest);
    if (!path[0] || !rest[0]) { oc_console_puts("write: no text\n"); return 1; }
    const char *resolved = shell_resolve_path_static(path);
    int fd = vfs_open(resolved, VFS_O_WRONLY | VFS_O_CREAT);
    if (fd < 0) { oc_console_puts("write: open failed\n"); return 1; }
    int len = (int)oc_strlen(rest);
    int n = vfs_write(fd, rest, len);
    char msg[60]; char num[20];
    oc_strcpy(msg, "wrote ");
    fmt_u64((u64)(n > 0 ? n : 0), num);
    oc_strcpy(msg + oc_strlen(msg), num);
    oc_strcpy(msg + oc_strlen(msg), " bytes\n");
    oc_console_puts(msg);
    vfs_close(fd);
    return 0;
}

/* grep: filter lines matching a pattern.
 * Usage: grep <pattern> [file]
 * If no file given, reads from shell stdin (pipe).
 * Prints only lines containing the pattern (case-sensitive). */
static int cmd_grep(const char *args) {
    if (!args || !args[0]) {
        oc_console_puts("usage: grep <pattern> [file]\n");
        return 1;
    }
    /* Parse pattern and optional filename. */
    char pattern[64];
    char fname[VFS_PATH_LEN];
    int i = 0, j = 0;
    while (args[i] && args[i] != ' ' && j < 63) { pattern[j++] = args[i++]; }
    pattern[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && j < VFS_PATH_LEN - 1) { fname[j++] = args[i++]; }
    fname[j] = 0;

    int plen = oc_strlen(pattern);
    if (plen == 0) {
        oc_console_puts("grep: empty pattern\n");
        return 1;
    }

    /* If a file is given, read from it; otherwise read from stdin (pipe). */
    if (fname[0]) {
        const char *resolved = shell_resolve_path_static(fname);
        int fd = vfs_open(resolved, VFS_O_RDONLY);
        if (fd < 0) {
            oc_console_puts("grep: cannot open file\n");
            return 1;
        }
        char buf[512];
        char line[256];
        int line_pos = 0;
        int n;
        while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
            for (int k = 0; k < n; k++) {
                if (buf[k] == '\n') {
                    line[line_pos] = 0;
                    /* Check if line contains pattern. */
                    int match = 0;
                    for (int p = 0; p + plen <= line_pos; p++) {
                        if (oc_strncmp(&line[p], pattern, plen) == 0) {
                            match = 1;
                            break;
                        }
                    }
                    if (match) {
                        oc_console_puts(line);
                        oc_console_putc('\n');
                    }
                    line_pos = 0;
                } else if (line_pos < 255) {
                    line[line_pos++] = buf[k];
                }
            }
        }
        /* Check last line (no trailing newline). */
        if (line_pos > 0) {
            line[line_pos] = 0;
            int match = 0;
            for (int p = 0; p + plen <= line_pos; p++) {
                if (oc_strncmp(&line[p], pattern, plen) == 0) {
                    match = 1;
                    break;
                }
            }
            if (match) {
                oc_console_puts(line);
                oc_console_putc('\n');
            }
        }
        vfs_close(fd);
    } else if (shell_has_stdin()) {
        /* Read from pipe stdin. */
        char buf[512];
        char line[256];
        int line_pos = 0;
        int n;
        while ((n = shell_read_stdin(buf, sizeof(buf))) > 0) {
            for (int k = 0; k < n; k++) {
                if (buf[k] == '\n') {
                    line[line_pos] = 0;
                    int match = 0;
                    for (int p = 0; p + plen <= line_pos; p++) {
                        if (oc_strncmp(&line[p], pattern, plen) == 0) {
                            match = 1;
                            break;
                        }
                    }
                    if (match) {
                        oc_console_puts(line);
                        oc_console_putc('\n');
                    }
                    line_pos = 0;
                } else if (line_pos < 255) {
                    line[line_pos++] = buf[k];
                }
            }
        }
        if (line_pos > 0) {
            line[line_pos] = 0;
            int match = 0;
            for (int p = 0; p + plen <= line_pos; p++) {
                if (oc_strncmp(&line[p], pattern, plen) == 0) {
                    match = 1;
                    break;
                }
            }
            if (match) {
                oc_console_puts(line);
                oc_console_putc('\n');
            }
        }
    } else {
        oc_console_puts("grep: no input (use a file or pipe)\n");
        return 1;
    }
    return 0;
}

/* ---- Registration ---- */

void file_cmds_register(void) {
    shell_register_command("ls",     cmd_ls,     "list directory (ls [path])");
    shell_register_command("cd",     cmd_cd,     "change directory (cd [path])");
    shell_register_command("pwd",    cmd_pwd,    "print working directory");
    shell_register_command("cat",    cmd_cat,    "print file contents (cat <file>)");
    shell_register_command("mkdir",  cmd_mkdir,  "make directory (mkdir <path>)");
    shell_register_command("rmdir",  cmd_rmdir,  "remove directory (rmdir <path>)");
    shell_register_command("touch",  cmd_touch,  "create empty file (touch <path>)");
    shell_register_command("rm",     cmd_rm,     "delete file (rm <file>)");
    shell_register_command("mv",     cmd_mv,     "move/rename file (mv <src> <dst>)");
    shell_register_command("cp",     cmd_cp,     "copy file (cp <src> <dst>)");
    shell_register_command("tree",   cmd_tree,   "tree view of directory (tree [path])");
    shell_register_command("df",     cmd_df,     "show disk/mount usage");
    shell_register_command("du",     cmd_du,     "directory usage (du [path])");
    shell_register_command("mount",  cmd_mount,  "list mounts (alias for mounts)");
    shell_register_command("umount", cmd_umount, "unmount (umount <path>)");
    shell_register_command("write",  cmd_write,  "write text to file (write <path> <text>)");
    shell_register_command("grep",   cmd_grep,   "filter lines matching pattern (grep <pattern> [file])");
}
