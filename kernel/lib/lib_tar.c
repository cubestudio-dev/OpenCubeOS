/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/tar.c
 * Purpose: streaming ustar reader, see tar.h.
 *
 * The parser buffers until it can act on whole 512-byte blocks:
 *   - header block: verify magic/checksum, extract name + size + type;
 *   - file body:    forward whole 512-byte blocks (truncated to the
 *                   file size) to the callback, then skip the padding;
 *   - two consecutive zero blocks (or clean EOF) end the archive.
 * GNU long-name ('L' typeflag) headers are consumed and used as the
 * path of the NEXT entry (mainstream GNU tar behaviour).
 */
#include "lib_tar.h"
#include "mem_heap.h"
#include "lib_string.h"

#include <stdint.h>

#define TAR_BLOCK 512

/* parser states */
#define TS_HEADER   0   /* collecting a header block                  */
#define TS_BODY     1   /* streaming file body blocks                 */
#define TS_PADDING  2  /* skipping the size padding to a block edge   */
#define TS_GNU_NAME 3  /* collecting a GNU 'L' long-name body         */
#define TS_END      4   /* archive finished                           */

struct lib_tar {
    lib_tar_file_fn cb;
    void *ctx;

    int  state;
    u8   buf[TAR_BLOCK];
    int  buf_len;          /* bytes currently in buf               */

    u64  file_size;        /* remaining bytes of the current file  */
    u64  file_size_total;
    int  file_type;
    int  zero_blocks;      /* consecutive zero blocks seen         */
    char cur_name[100];    /* current entry path (ustar field size,
                            * GNU long names extend this)         */
    char gnu_name[256];    /* GNU long-name scratch                */
};

/* Parse a NUL-padded octal field. Returns value or (u64)-1 on error. */
static u64 lib_tar_octal(const u8 *field, int len) {
    u64 v = 0;
    int started = 0;
    for (int i = 0; i < len; i++) {
        u8 c = field[i];
        if (c == 0 || c == ' ') {
            if (started) break;          /* terminator */
            continue;                    /* leading padding */
        }
        if (c < '0' || c > '7') return (u64)-1;
        started = 1;
        v = (v << 3) + (u64)(c - '0');
    }
    return started ? v : (u64)-1;
}

/* ustar header checksum: sum of all 512 bytes with the chksum field
 * taken as 8 spaces (unsigned variant, what GNU tar writes). */
static u32 lib_tar_hdr_csum(const u8 *b) {
    u32 sum = 0;
    for (int i = 0; i < TAR_BLOCK; i++) sum += b[i];
    /* the chksum field (148..155) was read as-is; subtract it and add
     * the 8-space baseline 8*0x20 */
    u32 stored = 0;
    for (int i = 148; i < 156; i++) stored += b[i];
    return sum - stored + 8u * 0x20u;
}

static int emit_file(lib_tar_t *t, const u8 *data, int len) {
    if (!t->cb) return 0;
    const char *name = (t->gnu_name[0]) ? t->gnu_name : t->cur_name;
    return t->cb(t->ctx, name, t->file_type, t->file_size_total, data, len);
}

/* Process one full 512-byte block (buf[0..511]). Returns 0 ok, <0 err. */
static int lib_tar_block(lib_tar_t *t) {
    int all_zero = 1;
    for (int i = 0; i < TAR_BLOCK; i++) {
        if (t->buf[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) {
        t->zero_blocks++;
        if (t->zero_blocks >= 2) t->state = TS_END;
        return 0;
    }
    t->zero_blocks = 0;

    if (t->state == TS_GNU_NAME) {
        /* previous header was a GNU 'L' long-name: buf holds the path */
        int n = 0;
        while (n < (int)sizeof(t->gnu_name) - 1 && t->buf[n]) {
            t->gnu_name[n] = (char)t->buf[n];
            n++;
        }
        t->gnu_name[n] = 0;
        t->state = TS_HEADER;
        return 0;
    }

    /* ---- header block ---- */
    static const char ustar_magic[6] = { 'u', 's', 't', 'a', 'r', 0 };
    if (memcmp(t->buf + 257, ustar_magic, 6) != 0) {
        /* also accept the pre-POSIX "ustar  \0" GNU magic */
        if (t->buf[257] != 'u' || t->buf[258] != 's' || t->buf[259] != 't' ||
            t->buf[260] != 'a' || t->buf[261] != 'r') {
            return OC_TAR_ERR_HEADER;
        }
    }
    u32 stored = (u32)lib_tar_octal(t->buf + 148, 8);
    if (stored == (u32)-1) return OC_TAR_ERR_CSUM;
    if (lib_tar_hdr_csum(t->buf) != stored) return OC_TAR_ERR_CSUM;

    u64 size = lib_tar_octal(t->buf + 124, 12);
    if (size == (u64)-1) return OC_TAR_ERR_SIZE;

    char type = (char)t->buf[156];

    /* copy the name (may contain a trailing '/') */
    int n = 0;
    while (n < 99 && t->buf[n]) { t->cur_name[n] = (char)t->buf[n]; n++; }
    t->cur_name[n] = 0;

    if (type == 'L') {
        /* GNU long name: the body holds the real path of the next hdr */
        t->gnu_name[0] = 0;
        t->file_size = size;
        t->file_size_total = size;
        t->file_type = OC_TAR_FILE;
        t->state = TS_GNU_NAME;
        return 0;
    }
    t->gnu_name[0] = 0;

    if (type == '5' || (n > 0 && t->cur_name[n - 1] == '/')) {
        /* directory entry: report it once (data == NULL), no body */
        t->file_type = OC_TAR_DIR;
        t->file_size_total = 0;
        t->file_size = 0;
        if (t->cb && t->cb(t->ctx, t->cur_name, OC_TAR_DIR, 0, NULL, 0) != 0)
            return OC_TAR_ERR_SINK;
        t->state = TS_HEADER;
        return 0;
    }
    if (type != '0' && type != '\0' && type != '7') {
        /* hard/symlink/special entries carry no body here: skip them */
        t->file_type = OC_TAR_FILE;
        t->file_size = 0;
        t->file_size_total = 0;
        t->state = (size > 0) ? TS_PADDING : TS_HEADER;
        return 0;
    }

    t->file_type = OC_TAR_FILE;
    t->file_size = size;
    t->file_size_total = size;
    t->state = (size > 0) ? TS_BODY : TS_HEADER;
    if (size == 0) {
        /* empty file: still emit the final empty slice */
        if (t->cb && t->cb(t->ctx, t->cur_name, OC_TAR_FILE, 0, NULL, 0) != 0)
            return OC_TAR_ERR_SINK;
    }
    return 0;
}

lib_tar_t *lib_tar_open(lib_tar_file_fn cb, void *ctx) {
    if (!cb) return NULL;
    lib_tar_t *t = (lib_tar_t *)kmalloc(sizeof(lib_tar_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->cb = cb;
    t->ctx = ctx;
    t->state = TS_HEADER;
    return t;
}

int lib_tar_feed(lib_tar_t *t, const u8 *data, int len) {
    if (!t || (!data && len > 0) || len < 0) return OC_TAR_ERR_ARGS;
    if (t->state == TS_END) return 0;

    int pos = 0;
    while (pos < len) {
        switch (t->state) {
        case TS_HEADER:
        case TS_GNU_NAME: {
            /* fill the block buffer */
            while (t->buf_len < TAR_BLOCK && pos < len) {
                t->buf[t->buf_len++] = data[pos++];
            }
            if (t->buf_len < TAR_BLOCK) return 0;
            int rc = lib_tar_block(t);
            if (rc != 0) return rc;
            t->buf_len = 0;
            break;
        }
        case TS_BODY: {
            if (t->file_size == 0) { t->state = TS_HEADER; break; }
            /* forward whole blocks (or the file tail) straight away */
            u64 want = t->file_size;
            u64 have = (u64)(len - pos);
            u64 take = (want < have) ? want : have;
            int rc = emit_file(t, data + pos, (int)take);
            if (rc != 0) return OC_TAR_ERR_SINK;
            t->file_size -= take;
            pos += (int)take;
            if (t->file_size == 0) {
                /* final empty slice for this file */
                if (emit_file(t, NULL, 0) != 0) return OC_TAR_ERR_SINK;
                /* padding to the next block boundary */
                u64 consumed = t->file_size_total;
                u64 pad = (TAR_BLOCK - (consumed & (TAR_BLOCK - 1))) &
                          (TAR_BLOCK - 1);
                if (pad > 0) {
                    t->state = TS_PADDING;
                    /* skip pad bytes from the stream */
                    u64 skip = (pad < (u64)(len - pos)) ? pad : (u64)(len - pos);
                    pos += (int)skip;
                    t->file_size = pad - skip;   /* remaining pad */
                    if (t->file_size > 0) return 0;
                    t->state = TS_HEADER;
                } else {
                    t->state = TS_HEADER;
                }
            }
            break;
        }
        case TS_PADDING: {
            u64 rem = t->file_size;            /* remaining pad bytes */
            u64 have = (u64)(len - pos);
            u64 skip = (rem < have) ? rem : have;
            pos += (int)skip;
            rem -= skip;
            t->file_size = rem;
            if (rem == 0) t->state = TS_HEADER;
            else return 0;
            break;
        }
        case TS_END:
            /* archive finished: silently ignore any remaining bytes
             * (records may be padded beyond the two zero blocks) */
            return 0;
        default:
            return OC_TAR_ERR_STATE;
        }
    }
    return 0;
}

int lib_tar_finish(lib_tar_t *t) {
    if (!t) return OC_TAR_ERR_ARGS;
    if (t->state == TS_END) return 0;
    if (t->state == TS_HEADER) return 0;       /* clean EOF after entries */
    /* GNU long-name body pending, file body incomplete or padding
     * missing: the archive was cut short */
    return OC_TAR_ERR_TRUNC;
}

void lib_tar_close(lib_tar_t *t) {
    if (t) kfree(t);
}
