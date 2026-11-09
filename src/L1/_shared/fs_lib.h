/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - shared L1 filesystem helper library.
 *
 * A header-only library (same style as l1_lib.h): each L1 app folder
 * compiles its own copy; there is no shared runtime to link.  It holds
 * the VFS on-wire structures and the small string helpers that the
 * 3.0.0-WP04 G5 file-tool apps (ls/cat/stat/du/... moved out of the
 * ush shell) need on top of _shared/l1_lib.h.
 *
 * Include from any app as:  #include "_shared/fs_lib.h"
 * (the app build adds -I src/L1 to the compiler invocation). */
#ifndef OC_L1_FS_LIB_H
#define OC_L1_FS_LIB_H

#include "_shared/l1_lib.h"

/* VFS node types (match kernel vfs.h: VFS_TYPE_FILE=1, DIR=2, DEVICE=3).
 * Moved verbatim from ush (the USH_ prefix is a historical name - the
 * numbers are the kernel contract). */
#define USH_TYPE_FILE 1
#define USH_TYPE_DIR  2
#define USH_TYPE_DEV  3

/* VFS on-wire structures - MUST match kernel layout (vfs.h,
 * VFS_NAME_LEN=64).  The kernel's sys_readdir/sys_stat memcpy
 * sizeof(vfs_dirent_t / vfs_stat_t) bytes into our buffer, so our
 * struct size must equal the kernel's.
 * WP-10-wp08fix1: fs_vfs_stat_t grew mode/uid/gid/nlink - mirrored here.
 * Moved verbatim from ush (original struct names kept to avoid any
 * transcription risk; the layout is the contract). */
struct ush_dirent {
    char name[64];
    int  type;
    u64  inode;
};
struct ush_stat {
    int  type;
    u64  size;
    u32  mode;
    u32  uid;
    u32  gid;
    u32  nlink;
    char name[64];
};

/* Bounded copy: copies at most n-1 bytes and always NUL-terminates.
 * (ush verbatim helper; the moved file tools use it for path buffers.) */
static inline void strncpy_(char *d, const char *s, int n) {
    int i = 0;
    while (s[i] && i < n - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* Substring search: 1 if needle appears in hay, 0 otherwise.
 * (ush verbatim helper; grep/awk use it.) */
static inline int strstr_(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    int hl = (int)strlen_(hay);
    int nl = (int)strlen_(needle);
    if (nl > hl) return 0;
    for (int i = 0; i <= hl - nl; i++) {
        int j = 0;
        while (j < nl && hay[i + j] == needle[j]) j++;
        if (j == nl) return 1;
    }
    return 0;
}

/* Bounded string compare: 0 if the first n chars are equal.
 * (ush verbatim helper; awk uses it.) */
static inline int strncmp_(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

/* Byte fill. (ush verbatim helper; the readdir/stat walkers zero their
 * on-wire structs before the kernel memcpy fills them.) */
static inline void memset_(void *d, int c, int n) {
    char *p = (char*)d;
    for (int i = 0; i < n; i++) p[i] = (char)c;
}

#endif /* OC_L1_FS_LIB_H */
