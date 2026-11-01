/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_iso.c
 * Purpose: ISO9660 read-only VFS backend - see ocse_iso.h.
 *
 * Node layout: the mount root's private holds the fs-wide ctx
 * (device index + root directory extent); every child node's private
 * holds an iso_node_t with its own extent. Directory extents are read
 * into a shared 32 KiB window (the install medium's directories are
 * far smaller; larger ones are truncated - declared limit).
 */
#include "ocse_iso.h"
#include "fs_vfs.h"
#include "driver_block_blk.h"
#include "mem_heap.h"
#include "lib_string.h"
#include <stdint.h>

#define ISO_DIRBUF_MAX  32768

typedef struct {
    int dev_idx;
    u32 root_lba;               /* 2048-sector units */
    u32 root_size;              /* bytes */
} iso_ctx_t;

typedef struct {
    iso_ctx_t* ctx;
    u32 lba;                    /* extent start (2048-sector units) */
    u32 size;                   /* extent size in bytes */
    int is_dir;
} iso_node_t;

static fs_vfs_fs_type_t  g_iso_fs_type;
static fs_vfs_fs_ops_t   g_iso_fs_ops;
static fs_vfs_file_ops_t g_iso_file_ops;
static fs_vfs_dir_ops_t  g_iso_dir_ops;

/* Shared directory-extent window + one-physical-sector read cache. */
static u8  g_dirbuf[ISO_DIRBUF_MAX];
static u8  g_secbuf[2048];
static i64 g_sec_cached = -1;

/* ---- raw extent I/O (pseudo 512 -> 2048) ---- */

static int iso_read_phys(iso_ctx_t* ctx, u32 lba2048, void* out) {
    if (lba2048 != (u32)g_sec_cached) {
        if (driver_block_read_sectors_raw(ctx->dev_idx,
                (u64)lba2048 * 4, 4, g_secbuf) != 0) {
            g_sec_cached = -1;
            return -1;
        }
        g_sec_cached = (i64)lba2048;
    }
    memcpy(out, g_secbuf, 2048);
    return 0;
}

/* Read `size` bytes of an extent starting at byte `off`. */
static int iso_read_extent(iso_ctx_t* ctx, u32 lba, u32 size, u32 off,
                           void* buf) {
    if (off >= size) return 0;
    u32 want = size - off;
    u8* dst = (u8*)buf;
    u8 tmp[2048];
    u32 done = 0;
    while (done < want) {
        u32 pos = off + done;
        u32 within = pos & 2047u;
        u32 chunk = 2048 - within;
        if (chunk > want - done) chunk = want - done;
        if (iso_read_phys(ctx, lba + (pos >> 11), tmp) != 0) return -1;
        memcpy(dst + done, tmp + within, chunk);
        done += chunk;
    }
    return 0;
}

/* Load a directory extent into the shared window (truncated to the
 * window size). Returns the usable size. */
static u32 iso_load_dir(iso_ctx_t* ctx, u32 lba, u32 size) {
    if (size == 0) return 0;
    u32 want = size;
    if (want > ISO_DIRBUF_MAX) want = ISO_DIRBUF_MAX;
    memset(g_dirbuf, 0, ISO_DIRBUF_MAX);
    u32 done = 0;
    while (done < want) {
        u32 chunk = want - done;
        if (chunk > 2048) chunk = 2048;
        if (iso_read_phys(ctx, lba + (done >> 11), g_dirbuf + done) != 0)
            return done;
        done += chunk;
    }
    return want;
}

/* ---- directory record helpers ---- */

static u32 rd32_(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Copy a record's file identifier into `out` (strip ";1"). */
static void iso_name_copy(const u8* nm, u32 nl, char* out, int cap) {
    u32 len = nl;
    if (len >= 2 && nm[len - 2] == ';') len -= 2;
    if (len >= 1 && nm[len - 1] == '.') len -= 1;    /* "NAME." -> "NAME" */
    if ((int)len > cap - 1) len = (u32)cap - 1;
    for (u32 i = 0; i < len; i++) out[i] = (char)nm[i];
    out[len] = 0;
}

static int iso_name_eq(const u8* nm, u32 nl, const char* want) {
    char tmp[VFS_NAME_LEN];
    iso_name_copy(nm, nl, tmp, sizeof(tmp));
    /* Both sides uppercased compare (medium names are uppercase). */
    u32 i = 0;
    for (;; i++) {
        char a = tmp[i];
        char b = want[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
        if (a == 0) return 1;
    }
}

/* ---- VFS hooks ---- */

static fs_vfs_node_t* iso_new_node(iso_ctx_t* ctx, const char* name,
                                   u32 lba, u32 size, int is_dir) {
    fs_vfs_node_t* n = fs_vfs_alloc_node(name,
        is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE, &g_iso_fs_type);
    if (!n) return NULL;
    n->size = size;
    iso_node_t* in = (iso_node_t*)kmalloc(sizeof(iso_node_t));
    if (!in) return NULL;
    in->ctx = ctx;
    in->lba = lba;
    in->size = size;
    in->is_dir = is_dir;
    n->private = in;
    return n;
}

static int iso_file_open(fs_vfs_node_t* node, int flags) {
    (void)node;
    if (flags & (VFS_O_WRONLY | VFS_O_RDWR)) return -1;  /* read-only fs */
    return 0;
}

static int iso_file_read(fs_vfs_node_t* node, u64 offset, void* buf, int size) {
    iso_node_t* in = (iso_node_t*)node->private;
    if (!in || in->is_dir) return -1;
    if (offset >= in->size) return 0;
    u32 want = (u32)size;
    if (want > in->size - (u32)offset) want = in->size - (u32)offset;
    if (iso_read_extent(in->ctx, in->lba, in->size, (u32)offset, buf) != 0)
        return -1;
    return (int)want;
}

static int iso_file_write(fs_vfs_node_t* node, u64 offset, const void* buf, int size) {
    (void)node; (void)offset; (void)buf; (void)size;
    return -1;                                       /* read-only fs */
}

static u64 iso_file_seek(fs_vfs_node_t* node, u64 offset, int whence) {
    u64 size = node->size;
    u64 off = offset;
    if (whence == VFS_SEEK_CUR) return offset;       /* VFS pre-applied */
    if (whence == VFS_SEEK_END) off = size + offset;
    if (off > size) off = size;
    return off;
}

static int iso_file_close(fs_vfs_node_t* node) { (void)node; return 0; }

static int iso_file_stat(fs_vfs_node_t* node, fs_vfs_stat_t* st) {
    st->type = node->type;
    st->size = node->size;
    st->mode = node->type == VFS_TYPE_DIR ? 0555 : 0444;
    st->uid = 0; st->gid = 0; st->nlink = 1;
    return 0;
}

static fs_vfs_node_t* iso_dir_lookup(fs_vfs_node_t* parent, const char* name) {
    iso_node_t* pin = (iso_node_t*)parent->private;
    if (!pin || !pin->is_dir) return NULL;
    u32 dsize = iso_load_dir(pin->ctx, pin->lba, pin->size);
    u32 off = 0;
    while (off + 34 <= dsize) {
        u8 rec_len = g_dirbuf[off];
        if (rec_len == 0) { off = (off / 2048 + 1) * 2048; continue; }
        if (off + rec_len > dsize) break;
        u32 nl = g_dirbuf[off + 32];
        u8 flags = g_dirbuf[off + 25];
        u32 ino = rd32_(g_dirbuf + off + 0);         /* extent . = 0, .. = 1 */
        if (nl > 0 && ino > 1 && iso_name_eq(g_dirbuf + off + 33, nl, name)) {
            char nm[VFS_NAME_LEN];
            iso_name_copy(g_dirbuf + off + 33, nl, nm, sizeof(nm));
            return iso_new_node(pin->ctx, nm, rd32_(g_dirbuf + off + 2),
                                rd32_(g_dirbuf + off + 10), (flags & 2) != 0);
        }
        off += rec_len;
    }
    return NULL;
}

static int iso_dir_readdir(fs_vfs_node_t* dir, int index, fs_vfs_dirent_t* entry) {
    iso_node_t* in = (iso_node_t*)dir->private;
    if (!in || !in->is_dir) return -1;
    u32 dsize = iso_load_dir(in->ctx, in->lba, in->size);
    u32 off = 0;
    int seen = 0;
    while (off + 34 <= dsize) {
        u8 rec_len = g_dirbuf[off];
        if (rec_len == 0) { off = (off / 2048 + 1) * 2048; continue; }
        if (off + rec_len > dsize) break;
        u32 nl = g_dirbuf[off + 32];
        u8 flags = g_dirbuf[off + 25];
        u32 ino = rd32_(g_dirbuf + off + 0);
        if (nl == 0 || ino <= 1) { off += rec_len; continue; }   /* self/parent */
        if (seen == index) {
            iso_name_copy(g_dirbuf + off + 33, nl, entry->name, VFS_NAME_LEN);
            entry->type = (flags & 2) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
            entry->inode = ino;
            return 0;
        }
        seen++;
        off += rec_len;
    }
    return -1;                                       /* past the end */
}

static int iso_dir_mkdir(fs_vfs_node_t* parent, const char* name) {
    (void)parent; (void)name; return -1;             /* read-only fs */
}
static int iso_dir_rmdir(fs_vfs_node_t* parent, const char* name) {
    (void)parent; (void)name; return -1;
}
static int iso_dir_unlink(fs_vfs_node_t* parent, const char* name) {
    (void)parent; (void)name; return -1;
}
static int iso_dir_rename(fs_vfs_node_t* parent, const char* o, const char* n) {
    (void)parent; (void)o; (void)n; return -1;
}
static int iso_dir_create(fs_vfs_node_t* parent, const char* name) {
    (void)parent; (void)name; return -1;
}

/* ---- mount / unmount ---- */

static fs_vfs_node_t* iso_fs_mount(const char* device) {
    if (!device || !device[0]) return NULL;
    int dev_idx = driver_block_find_device(device);
    if (dev_idx < 0) return NULL;
    driver_block_device_t* dev = driver_block_get_device(dev_idx);
    if (!dev || !dev->present) return NULL;

    /* PVD at 2048-sector 16: type 1, "CD001". */
    u8 pvd[2048];
    if (driver_block_read_sectors_raw(dev_idx, 16u * 4, 4, pvd) != 0)
        return NULL;
    if (pvd[0] != 1) return NULL;
    if (pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' ||
        pvd[4] != '0' || pvd[5] != '1')
        return NULL;
    const u8* root = pvd + 156;
    if (root[0] == 0 || root[0] > 128) return NULL;

    iso_ctx_t* ctx = (iso_ctx_t*)kmalloc(sizeof(iso_ctx_t));
    if (!ctx) return NULL;
    ctx->dev_idx = dev_idx;
    ctx->root_lba = rd32_(root + 2);
    ctx->root_size = rd32_(root + 10);

    fs_vfs_node_t* root_node = iso_new_node(ctx, "/", ctx->root_lba,
                                            ctx->root_size, 1);
    if (!root_node) { kfree(ctx); return NULL; }
    return root_node;
}

/* Recursively free the cached subtree (the fs owns its nodes: the
 * mount root and every node a lookup attached below it). Mirrors
 * fs_ext4_free_subtree (BUG-0196 pattern). */
static void iso_free_subtree(fs_vfs_node_t* node) {
    if (!node) return;
    fs_vfs_node_t* child = node->first_child;
    while (child) {
        fs_vfs_node_t* next = child->next_sibling;
        iso_free_subtree(child);
        child = next;
    }
    iso_node_t* in = (iso_node_t*)node->private;
    if (in) kfree(in);
    kfree(node);
}

static int iso_fs_unmount(fs_vfs_node_t* root) {
    if (!root) return -1;
    iso_node_t* ino = (iso_node_t*)root->private;
    if (ino && ino->ctx) kfree(ino->ctx);
    iso_free_subtree(root);
    return 0;
}

void ocse_iso_init(void) {
    memset(&g_iso_fs_type, 0, sizeof(g_iso_fs_type));
    strcpy(g_iso_fs_type.name, "iso9660");
    g_iso_fs_ops.mount   = iso_fs_mount;
    g_iso_fs_ops.unmount = iso_fs_unmount;
    g_iso_file_ops.open  = iso_file_open;
    g_iso_file_ops.read  = iso_file_read;
    g_iso_file_ops.write = iso_file_write;
    g_iso_file_ops.seek  = iso_file_seek;
    g_iso_file_ops.close = iso_file_close;
    g_iso_file_ops.stat  = iso_file_stat;
    g_iso_dir_ops.mkdir   = iso_dir_mkdir;
    g_iso_dir_ops.rmdir   = iso_dir_rmdir;
    g_iso_dir_ops.readdir = iso_dir_readdir;
    g_iso_dir_ops.lookup  = iso_dir_lookup;
    g_iso_dir_ops.unlink  = iso_dir_unlink;
    g_iso_dir_ops.rename  = iso_dir_rename;
    g_iso_dir_ops.create  = iso_dir_create;
    /* The fs-wide type every looked-up node carries (node->fs_type);
     * the mount root is re-pointed at the registry entry by
     * fs_vfs_mount, so BOTH structures must carry the full ops. */
    g_iso_fs_type.fs_ops   = &g_iso_fs_ops;
    g_iso_fs_type.file_ops = &g_iso_file_ops;
    g_iso_fs_type.dir_ops  = &g_iso_dir_ops;
    fs_vfs_register_fs("iso9660", &g_iso_fs_ops, &g_iso_file_ops, &g_iso_dir_ops);
    fs_vfs_set_fs_case_insensitive("iso9660", 1);
}
